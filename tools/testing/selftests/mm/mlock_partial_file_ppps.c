// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <stdint.h>
#include <sys/klog.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define MAP_BYTES		(2UL * 1024 * 1024)
#define SHMEM_MTHP_BYTES	(64UL * 1024)
#define SHMEM_MTHP_POLICY	\
	"/sys/kernel/mm/transparent_hugepage/hugepages-64kB/shmem_enabled"
#define KPF_COMPOUND_HEAD	15
#define KPF_COMPOUND_TAIL	16
#define TEST_COMM		"mlock-ppps"
#define HOLDER_COUNT		1

#ifndef SYSLOG_ACTION_READ_ALL
#define SYSLOG_ACTION_READ_ALL	3
#endif
#ifndef SYSLOG_ACTION_SIZE_BUFFER
#define SYSLOG_ACTION_SIZE_BUFFER 10
#endif

static char saved_policy[32];
static bool policy_changed;

static bool selected_policy(char *policy, size_t size)
{
	char buffer[256];
	char *begin, *end;
	size_t length;
	FILE *file;

	file = fopen(SHMEM_MTHP_POLICY, "re");
	if (!file)
		return false;
	if (!fgets(buffer, sizeof(buffer), file)) {
		fclose(file);
		return false;
	}
	fclose(file);
	begin = strchr(buffer, '[');
	end = begin ? strchr(begin, ']') : NULL;
	if (!begin || !end)
		return false;
	length = end - ++begin;
	if (!length || length >= size)
		return false;
	memcpy(policy, begin, length);
	policy[length] = '\0';
	return true;
}

static bool set_policy(const char *policy)
{
	ssize_t length = strlen(policy);
	int fd;
	bool ok;

	fd = open(SHMEM_MTHP_POLICY, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	ok = write(fd, policy, length) == length;
	close(fd);
	return ok;
}

static void restore_policy(void)
{
	if (policy_changed && !set_policy(saved_policy))
		ksft_print_msg("failed to restore %s to %s: %s\n",
			       SHMEM_MTHP_POLICY, saved_policy, strerror(errno));
}

static bool read_u64(int fd, uint64_t index, uint64_t *value)
{
	off_t offset = (off_t)(index * sizeof(*value));

	return pread(fd, value, sizeof(*value), offset) == sizeof(*value);
}

static char *read_kernel_log(size_t *length)
{
	char *log;
	int size, bytes;

	size = klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);
	if (size < 0)
		return NULL;
	log = malloc(size + 1);
	if (!log)
		return NULL;
	bytes = klogctl(SYSLOG_ACTION_READ_ALL, log, size);
	if (bytes < 0) {
		free(log);
		return NULL;
	}
	log[bytes] = '\0';
	*length = bytes;
	return log;
}

static bool find_compound_file_page(unsigned char *mapping, size_t *target)
{
	int pagemap_fd = -1, flags_fd = -1;
	size_t offset;
	bool found = false;

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
	flags_fd = open("/proc/kpageflags", O_RDONLY | O_CLOEXEC);
	if (pagemap_fd < 0 || flags_fd < 0)
		goto out;
	for (offset = 0; offset < MAP_BYTES; offset += PROCESS_PAGE_SIZE) {
		uint64_t entry, flags, pfn;

		if (!read_u64(pagemap_fd,
			      (uintptr_t)(mapping + offset) / PROCESS_PAGE_SIZE,
			      &entry) || !(entry & PAGEMAP_PRESENT) ||
		    !(entry & PAGEMAP_FILE_SHARED))
			goto out;
		pfn = ppps_native_pfn(entry & PAGEMAP_PFN_MASK);
		if (!pfn || !read_u64(flags_fd, pfn, &flags))
			goto out;
		if (flags & ((UINT64_C(1) << KPF_COMPOUND_HEAD) |
			     (UINT64_C(1) << KPF_COMPOUND_TAIL))) {
			*target = offset;
			found = true;
			break;
		}
	}
out:
	if (flags_fd >= 0)
		close(flags_fd);
	if (pagemap_fd >= 0)
		close(pagemap_fd);
	return found;
}

static int mlock_onfault(void *address, size_t length)
{
	return syscall(SYS_mlock2, address, length, MLOCK_ONFAULT);
}

static int child_fault(unsigned char *mapping, size_t target, int report_fd,
			 bool drop_all)
{
	unsigned char stage = 0;
	unsigned char value;
	unsigned int prefault = 0;
	size_t offset;

	if (!drop_all) {
		size_t base = target & ~(SHMEM_MTHP_BYTES - 1);

		for (offset = base; offset < base + SHMEM_MTHP_BYTES;
		     offset += PROCESS_PAGE_SIZE)
			prefault += mapping[offset];
	}

	if (!drop_all && !prefault)
		stage = 7;
	else if (prctl(PR_SET_NAME, TEST_COMM, 0, 0, 0))
		stage = 1;
	else if (madvise(drop_all ? mapping : mapping + target,
			 drop_all ? MAP_BYTES : PROCESS_PAGE_SIZE, MADV_DONTNEED))
		stage = 2;
	else if (ppps_page_present(mapping + target))
		stage = 3;
	else if (mlock_onfault(mapping, MAP_BYTES))
		stage = 4;
	if (!write_full(report_fd, &stage, sizeof(stage)) || stage)
		return stage ? stage : 5;

	/*
	 * The partial case leaves one PTE; the complete case refaults the only PTE
	 * dropped from the prefaulted compound folio.  Both exercise the natural
	 * end and early-stop outcomes of the PPPS rmap walk.
	 */
	value = mapping[target];
	return value ? 0 : 6;
}

static int run_child_fault(unsigned char *mapping, size_t target, bool drop_all,
			   unsigned char *stage, bool *ready, pid_t *child)
{
	int pipefd[2], status;

	if (pipe(pipefd))
		return -1;
	*child = fork();
	if (*child < 0)
		return -1;
	if (!*child) {
		close(pipefd[0]);
		_exit(child_fault(mapping, target, pipefd[1], drop_all));
	}
	close(pipefd[1]);
	*ready = read_full(pipefd[0], stage, sizeof(*stage));
	close(pipefd[0]);
	if (waitpid(*child, &status, 0) != *child)
		return -1;
	return status;
}

static bool start_mapping_holders(unsigned char *mapping, size_t target,
				  pid_t holders[HOLDER_COUNT],
				  int *release_fd)
{
	unsigned char byte = 0;
	unsigned int checksum = 0;
	int ready[2], release[2];
	unsigned int i;

	memset(holders, 0, sizeof(*holders) * HOLDER_COUNT);
	if (pipe(ready))
		return false;
	if (pipe(release)) {
		close(ready[0]);
		close(ready[1]);
		return false;
	}
	for (i = 0; i < HOLDER_COUNT; i++) {
		holders[i] = fork();
		if (holders[i] < 0)
			break;
		if (!holders[i]) {
			size_t base = target & ~(SHMEM_MTHP_BYTES - 1);
			unsigned int child_checksum = 0;
			size_t offset;

			close(ready[0]);
			close(release[1]);
			for (offset = base; offset < base + SHMEM_MTHP_BYTES;
			     offset += PROCESS_PAGE_SIZE)
				child_checksum += mapping[offset];
			if (!child_checksum ||
			    !write_full(ready[1], &child_checksum,
					sizeof(child_checksum)))
				_exit(1);
			close(ready[1]);
			while (read(release[0], &byte, sizeof(byte)) < 0 &&
			       errno == EINTR)
				;
			_exit(0);
		}
	}
	close(ready[1]);
	close(release[0]);
	if (i == HOLDER_COUNT) {
		for (i = 0; i < HOLDER_COUNT; i++) {
			if (!read_full(ready[0], &checksum, sizeof(checksum)) ||
			    !checksum)
				break;
		}
	}
	close(ready[0]);
	if (i == HOLDER_COUNT) {
		*release_fd = release[1];
		return true;
	}
	close(release[1]);
	for (i = 0; i < HOLDER_COUNT; i++) {
		if (holders[i] > 0)
			waitpid(holders[i], NULL, 0);
	}
	return false;
}

static bool stop_mapping_holders(pid_t holders[HOLDER_COUNT], int release_fd)
{
	unsigned int i;
	bool clean = close(release_fd) == 0;

	for (i = 0; i < HOLDER_COUNT; i++) {
		int status;

		if (waitpid(holders[i], &status, 0) != holders[i] ||
		    !WIFEXITED(status) || WEXITSTATUS(status))
			clean = false;
	}
	return clean;
}

static int run_test(void)
{
	unsigned char *mapping;
	unsigned char stage = 0xff;
	char needle[96];
	char *log_before, *log_after;
	const char *new_log;
	size_t before_len, after_len;
	size_t offset, target;
	int status;
	pid_t child;
	pid_t holders[HOLDER_COUNT];
	int release_fd = -1;
	bool holders_clean = false;
	bool holders_ready;
	bool ready;

	ppps_require_compat();
	ksft_print_header();
	if (!selected_policy(saved_policy, sizeof(saved_policy)))
		ksft_exit_skip("64K shmem mTHP policy is unavailable\n");
	if (strcmp(saved_policy, "always")) {
		if (!set_policy("always"))
			ksft_exit_skip("cannot enable 64K shmem mTHP policy: %s\n",
				       strerror(errno));
		policy_changed = true;
		atexit(restore_policy);
	}

	mapping = mmap(NULL, MAP_BYTES, PROT_READ | PROT_WRITE,
		       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("create shared shmem mapping: %s\n",
				   strerror(errno));
	if (madvise(mapping, MAP_BYTES, MADV_HUGEPAGE)) {
		if (errno == EINVAL)
			ksft_exit_skip("compat VMA cannot request shmem THP\n");
		ksft_exit_fail_msg("enable huge pages on mapping: %s\n",
				   strerror(errno));
	}
	for (offset = 0; offset < MAP_BYTES; offset += PROCESS_PAGE_SIZE)
		mapping[offset] = (unsigned char)(offset / PROCESS_PAGE_SIZE + 1);
	if (!find_compound_file_page(mapping, &target))
		ksft_exit_skip("could not allocate an observable compound shmem folio\n");
	log_before = read_kernel_log(&before_len);
	if (!log_before)
		ksft_exit_skip("cannot read the kernel log: %s\n", strerror(errno));

	ksft_set_plan(7);
	ksft_test_result(getpagesize() == PROCESS_PAGE_SIZE,
			 "run with 4K process geometry\n");
	ksft_test_result(true,
			 "allocate a file-backed compound folio at offset %#zx\n",
			 target);
	status = run_child_fault(mapping, target, true, &stage, &ready, &child);
	ksft_test_result(ready && !stage,
			 "drop child PTEs and arm MLOCK_ONFAULT without populating them\n");
	ksft_test_result(status >= 0 && WIFEXITED(status) && !WEXITSTATUS(status),
			 "fault a partially mapped locked large file folio # status=%#x\n",
			 status);

	stage = 0xff;
	holders_ready = start_mapping_holders(mapping, target, holders,
					      &release_fd);
	if (holders_ready) {
		status = run_child_fault(mapping, target, false, &stage, &ready,
					 &child);
		holders_clean = stop_mapping_holders(holders, release_fd);
	} else {
		status = -1;
		ready = false;
	}
	ksft_test_result(holders_ready && ready && !stage,
			 "hold peer mappings, drop one PTE and arm MLOCK_ONFAULT\n");
	ksft_test_result(status >= 0 && WIFEXITED(status) && !WEXITSTATUS(status) &&
			 holders_clean,
			 "fault the final PTE of a locked large file folio # status=%#x\n",
			 status);
	log_after = read_kernel_log(&after_len);
	if (!log_after)
		ksft_exit_fail_msg("read kernel log after fault: %s\n",
				   strerror(errno));
	new_log = after_len >= before_len &&
		  !memcmp(log_after, log_before, before_len) ?
		  log_after + before_len : log_after;
	snprintf(needle, sizeof(needle), "BUG: scheduling while atomic: %s/",
		 TEST_COMM);
	ksft_test_result(!strstr(new_log, needle),
			 "leave the child's scheduler/lock state balanced\n");
	free(log_after);
	free(log_before);
	munmap(mapping, MAP_BYTES);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
