// SPDX-License-Identifier: GPL-2.0
/*
 * A native (16K) reader walks the page tables of a 4K compat child through
 * /proc/<pid>/smaps, pagemap and clear_refs: RSS, Referenced and present
 * PTEs are reported in the child's 4K units and the walks leave the child's
 * page contents intact.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define TEST_PAGES	32
#define MAPPING_SIZE	(TEST_PAGES * PROCESS_PAGE_SIZE)
#define RESERVE_SIZE	(MAPPING_SIZE + 2 * PROCESS_PAGE_SIZE)
#define PAGEMAP_ENTRY_SIZE sizeof(uint64_t)

struct child_status {
	long page_size;
	unsigned long address;
	bool mapped;
	bool isolated;
};

static bool vma_span(const void *address, unsigned long *span)
{
	unsigned long target = (unsigned long)address;
	unsigned long start, end;
	char *line = NULL;
	size_t capacity = 0;
	bool found = false;
	FILE *maps;

	maps = fopen("/proc/self/maps", "re");
	if (!maps)
		return false;
	while (getline(&line, &capacity, maps) >= 0) {
		if (sscanf(line, "%lx-%lx", &start, &end) != 2)
			continue;
		if (target >= start && target < end) {
			*span = end - start;
			found = true;
			break;
		}
	}
	free(line);
	fclose(maps);
	return found;
}

static bool read_smaps(pid_t pid, unsigned long address,
			unsigned long *rss_bytes,
			unsigned long *referenced_bytes)
{
	unsigned long start, end, value_kb;
	char path[64];
	char *line = NULL;
	size_t capacity = 0;
	bool found_rss = false;
	bool found_referenced = false;
	bool in_target = false;
	FILE *smaps;

	snprintf(path, sizeof(path), "/proc/%d/smaps", pid);
	smaps = fopen(path, "re");
	if (!smaps)
		return false;
	while (getline(&line, &capacity, smaps) >= 0) {
		if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
			if (in_target)
				break;
			in_target = address >= start && address < end;
			continue;
		}
		if (!in_target)
			continue;
		if (sscanf(line, "Rss: %lu kB", &value_kb) == 1) {
			*rss_bytes = value_kb * 1024;
			found_rss = true;
		} else if (sscanf(line, "Referenced: %lu kB", &value_kb) == 1) {
			*referenced_bytes = value_kb * 1024;
			found_referenced = true;
		}
	}
	free(line);
	fclose(smaps);
	return found_rss && found_referenced;
}

static bool count_present_pages(pid_t pid, unsigned long address,
				unsigned int *present)
{
	uint64_t entries[TEST_PAGES];
	char path[64];
	off_t offset;
	ssize_t bytes;
	unsigned int i;
	int fd;

	snprintf(path, sizeof(path), "/proc/%d/pagemap", pid);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	offset = (address / PROCESS_PAGE_SIZE) * PAGEMAP_ENTRY_SIZE;
	bytes = pread(fd, entries, sizeof(entries), offset);
	close(fd);
	if (bytes != sizeof(entries))
		return false;
	*present = 0;
	for (i = 0; i < TEST_PAGES; i++) {
		if (entries[i] & PAGEMAP_PRESENT)
			(*present)++;
	}
	return true;
}

static bool clear_references(pid_t pid)
{
	char path[64];
	int fd;
	bool cleared;

	snprintf(path, sizeof(path), "/proc/%d/clear_refs", pid);
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	cleared = write(fd, "1\n", 2) == 2;
	close(fd);
	return cleared;
}

static int run_compat_child(int ready_fd, int command_fd)
{
	struct child_status status = {
		.page_size = sysconf(_SC_PAGESIZE),
	};
	unsigned long span = 0;
	unsigned char *mapping;
	unsigned char *reservation;
	unsigned char command;
	bool preserved = true;
	unsigned int i;

	reservation = mmap(NULL, RESERVE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	mapping = reservation == MAP_FAILED ? MAP_FAILED :
		mmap(reservation + PROCESS_PAGE_SIZE, MAPPING_SIZE,
		     PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (mapping != MAP_FAILED) {
		madvise(mapping, MAPPING_SIZE, MADV_NOHUGEPAGE);
		for (i = 0; i < TEST_PAGES; i++)
			mapping[i * PROCESS_PAGE_SIZE] = 0x40 + i;
		status.address = (unsigned long)mapping;
		status.mapped = true;
		status.isolated = vma_span(mapping, &span) &&
				  span == MAPPING_SIZE;
	}

	if (!write_full(ready_fd, &status, sizeof(status)) ||
	    !read_full(command_fd, &command, sizeof(command)))
		preserved = false;
	if (mapping == MAP_FAILED) {
		preserved = false;
	} else {
		for (i = 0; i < TEST_PAGES; i++) {
			if (mapping[i * PROCESS_PAGE_SIZE] !=
			    (unsigned char)(0x40 + i)) {
				preserved = false;
				break;
			}
		}
		munmap(reservation, RESERVE_SIZE);
	}
	return preserved ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int run_parent(void)
{
	struct child_status child = {};
	unsigned long referenced = 0;
	unsigned long referenced_after = 0;
	unsigned long rss = 0;
	unsigned long rss_after = 0;
	unsigned int present = 0;
	int ready_pipe[2];
	int command_pipe[2];
	unsigned char command = 1;
	bool child_ready;
	bool smaps_ok;
	bool pagemap_ok;
	bool clear_ok;
	bool smaps_after_ok;
	bool child_waited;
	int child_wait_status = 0;
	char ready_fd[16];
	char command_fd[16];
	pid_t pid;

	ksft_print_header();
	ksft_set_plan(9);
	if (pipe(ready_pipe) || pipe(command_pipe))
		ksft_exit_fail_msg("pipe failed: %s\n", strerror(errno));
	pid = fork();
	if (pid < 0)
		ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
	if (!pid) {
		close(ready_pipe[0]);
		close(command_pipe[1]);
		snprintf(ready_fd, sizeof(ready_fd), "%d", ready_pipe[1]);
		snprintf(command_fd, sizeof(command_fd), "%d", command_pipe[0]);
		ppps_execl(true, NULL, "--child", ready_fd, command_fd, NULL);
		_exit(127);
	}

	close(ready_pipe[1]);
	close(command_pipe[0]);
	child_ready = read_full(ready_pipe[0], &child, sizeof(child));
	close(ready_pipe[0]);
	ksft_test_result(child_ready, "start compat child process\n");
	if (!child_ready) {
		close(command_pipe[1]);
		waitpid(pid, NULL, 0);
		ksft_exit_fail_msg("compat child did not report status\n");
	}
	ksft_print_msg("proc reader page size: %ld\n", sysconf(_SC_PAGESIZE));
	ksft_test_result(child.page_size == PROCESS_PAGE_SIZE,
			 "child process uses 4K pages\n");
	ksft_test_result(child.mapped && child.isolated,
			 "map an isolated anonymous VMA in the child\n");

	smaps_ok = read_smaps(pid, child.address, &rss, &referenced);
	ksft_test_result(smaps_ok && rss == MAPPING_SIZE,
			 "smaps reports child RSS (%lu bytes)\n", rss);
	ksft_test_result(smaps_ok && referenced == MAPPING_SIZE,
			 "smaps reports referenced child pages (%lu bytes)\n",
			 referenced);
	pagemap_ok = count_present_pages(pid, child.address, &present);
	ksft_test_result(pagemap_ok && present == TEST_PAGES,
			 "pagemap reports all child PTEs (%u pages)\n", present);
	clear_ok = clear_references(pid);
	ksft_test_result(clear_ok, "clear_refs accepts the child mm\n");
	smaps_after_ok = read_smaps(pid, child.address, &rss_after,
				    &referenced_after);
	ksft_test_result(smaps_after_ok && rss_after == MAPPING_SIZE &&
			 referenced_after == 0,
			 "clear_refs clears child references (Rss: %lu, Referenced: %lu bytes)\n",
			 rss_after, referenced_after);

	write_full(command_pipe[1], &command, sizeof(command));
	close(command_pipe[1]);
	do {
		child_waited = waitpid(pid, &child_wait_status, 0) == pid;
	} while (!child_waited && errno == EINTR);
	ksft_test_result(child_waited && WIFEXITED(child_wait_status) &&
			 WEXITSTATUS(child_wait_status) == EXIT_SUCCESS,
			 "proc walks preserve child page contents\n");
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode)
		exec_native(argv[0], "--parent", NULL);
	if (argc == 2 && !strcmp(mode, "--parent"))
		return run_parent();
	if (argc == 4 && !strcmp(mode, "--child"))
		return run_compat_child(atoi(argv[2]), atoi(argv[3]));
	return EXIT_FAILURE;
}
