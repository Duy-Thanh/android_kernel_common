// SPDX-License-Identifier: GPL-2.0
/*
 * In an isolated memory cgroup with MGLRU disabled, memory.reclaim keeps a
 * file folio resident when only its final 4K slice was referenced by a
 * compat process.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/stat.h>

#include "kselftest_ppps.h"

#define NR_SLICES PPPS_SLICES
#define MAP_SIZE (PROCESS_PAGE_SIZE * NR_SLICES)
#define FILLER_SIZE (2 * 1024 * 1024UL)

static int failures;
static int test_no;

static void result(int pass, const char *name)
{
	printf("%s %d - %s\n", pass ? "ok" : "not ok", ++test_no, name);
	if (!pass)
		failures++;
}

static int write_text(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	int saved;

	if (fd < 0)
		return -1;
	if (!write_full(fd, text, strlen(text))) {
		saved = errno;
		close(fd);
		errno = saved;
		return -1;
	}
	return close(fd);
}

static int read_text(const char *path, char *buf, size_t size)
{
	ssize_t n;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	n = read(fd, buf, size - 1);
	if (n < 0) {
		int saved = errno;

		close(fd);
		errno = saved;
		return -1;
	}
	buf[n] = '\0';
	close(fd);
	return 0;
}

static int run_test(const char *file)
{
	static const char cgdir[] = "/sys/fs/cgroup/rmap-file-referenced-ppps";
	static const char lru_gen_path[] = "/sys/kernel/mm/lru_gen/enabled";
	static const char root_procs[] = "/sys/fs/cgroup/cgroup.procs";
	static const char subtree[] = "/sys/fs/cgroup/cgroup.subtree_control";
	char procs[sizeof(cgdir) + sizeof("/cgroup.procs")];
	char reclaim[sizeof(cgdir) + sizeof("/memory.reclaim")];
	char lru_gen_value[32];
	unsigned char vec[NR_SLICES] = {};
	unsigned char checksum = 0;
	unsigned char *filler = MAP_FAILED;
	char pid[32];
	unsigned char *map = MAP_FAILED;
	int fd = -1;
	size_t i;
	int rc;
	int filler_rc;
	int in_cgroup = 0;
	int reclaim_errno;
	int restore_lru_gen = 0;

	printf("TAP version 13\n1..6\n");

	rc = read_text(lru_gen_path, lru_gen_value, sizeof(lru_gen_value));
	if (!rc) {
		restore_lru_gen = 1;
		rc = write_text(lru_gen_path, "0");
	}
	if (rc)
		perror("disable MGLRU");
	result(rc == 0, "isolate rmap reclaim from MGLRU aging");
	if (rc)
		goto out;

	if (write_text(subtree, "+memory\n") && errno != EBUSY) {
		perror("enable memory controller");
		result(0, "enter an isolated memory cgroup");
		goto out;
	}
	if (mkdir(cgdir, 0755) && errno != EEXIST) {
		perror("mkdir cgroup");
		result(0, "enter an isolated memory cgroup");
		goto out;
	}
	snprintf(procs, sizeof(procs), "%s/cgroup.procs", cgdir);
	snprintf(reclaim, sizeof(reclaim), "%s/memory.reclaim", cgdir);
	snprintf(pid, sizeof(pid), "%ld", (long)getpid());
	rc = write_text(procs, pid);
	if (rc)
		perror("write cgroup.procs");
	result(rc == 0, "enter an isolated memory cgroup");
	if (rc)
		goto out;
	in_cgroup = 1;

	fd = open(file, O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600);
	if (fd < 0 || ftruncate(fd, MAP_SIZE)) {
		perror("prepare file");
		result(0, "fault the target folio before cold reclaim filler");
		goto out;
	}

	map = mmap(NULL, MAP_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		result(0, "fault the target folio before cold reclaim filler");
		goto out;
	}
	for (i = 0; i < NR_SLICES; i++)
		checksum ^= map[i * PROCESS_PAGE_SIZE];
	printf("# target checksum=%u\n", checksum);

	filler = mmap(NULL, FILLER_SIZE, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (filler != MAP_FAILED) {
		memset(filler, 0x5a, FILLER_SIZE);
		filler_rc = madvise(filler, FILLER_SIZE, MADV_FREE);
	} else {
		filler_rc = -1;
	}
	rc = mincore(map, MAP_SIZE, vec);
	result(filler != MAP_FAILED && filler_rc == 0 && rc == 0 &&
	       (vec[0] & 1) && (vec[1] & 1) &&
	       (vec[2] & 1) && (vec[3] & 1),
	       "fault the target folio before cold reclaim filler");

	rc = madvise(map, MAP_SIZE, MADV_COLD);
	if (rc)
		perror("madvise(MADV_COLD)");
	result(rc == 0, "clear accessed state for the full mapping");

	checksum ^= map[3 * PROCESS_PAGE_SIZE];
	printf("# hot checksum=%u\n", checksum);
	memset(vec, 0, sizeof(vec));
	rc = mincore(map, MAP_SIZE, vec);
	result(rc == 0 && (vec[3] & 1), "mark only the final 4K slice hot");

	errno = 0;
	rc = write_text(reclaim, "524288");
	reclaim_errno = rc ? errno : 0;
	if (rc && reclaim_errno != EAGAIN)
		perror("write memory.reclaim");
	printf("# memory.reclaim rc=%d errno=%d\n", rc, reclaim_errno);
	memset(vec, 0, sizeof(vec));
	rc = mincore(map, MAP_SIZE, vec);
	printf("# mincore after reclaim: %u %u %u %u\n",
	       vec[0] & 1, vec[1] & 1, vec[2] & 1, vec[3] & 1);
	result((reclaim_errno == 0 || reclaim_errno == EAGAIN) &&
	       rc == 0 && (vec[3] & 1),
	       "reclaim preserves a folio referenced by its final slice");

out:
	if (map != MAP_FAILED)
		munmap(map, MAP_SIZE);
	if (filler != MAP_FAILED)
		munmap(filler, FILLER_SIZE);
	if (in_cgroup) {
		if (write_text(root_procs, pid))
			perror("leave memory cgroup");
		else if (rmdir(cgdir))
			perror("remove memory cgroup");
	}
	if (restore_lru_gen && write_text(lru_gen_path, lru_gen_value))
		perror("restore MGLRU");
	if (fd >= 0)
		close(fd);
	unlink(file);
	printf("# Totals: pass:%d fail:%d\n", test_no - failures, failures);
	return failures ? 1 : 0;
}

/*
 * Like PPPS_COMPAT_MAIN, but the FILE operand travels through the compat
 * re-exec: "<test> FILE" becomes "<test> --run FILE".
 */
int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (mode && argc == 3 && !strcmp(mode, PPPS_RUN_FLAG)) {
		ppps_require_compat();
		return run_test(argv[2]);
	}
	if (mode || argc != 2) {
		printf("TAP version 13\n1..6\nBail out! usage: %s FILE\n",
		       argv[0]);
		return 1;
	}
	if (!ppps_is_compat_process())
		exec_compat(argv[0], PPPS_RUN_FLAG, argv[1], NULL);
	return run_test(argv[1]);
}
