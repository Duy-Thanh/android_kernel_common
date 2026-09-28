// SPDX-License-Identifier: GPL-2.0
/*
 * getrusage() ru_maxrss of a 4K compat process and of its 4K and native
 * children matches the VmHWM each process reports, in common kB units.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define PARENT_SIZE	(32UL * 1024 * 1024)
#define CHILD_4K_SIZE	(24UL * 1024 * 1024)
#define CHILD_NATIVE_SIZE (40UL * 1024 * 1024)

struct child_report {
	long page_size;
	unsigned long hwm_kb;
};

static bool read_hwm(unsigned long *hwm_kb)
{
	long value = ppps_status_kb(0, "VmHWM");

	if (value < 0)
		return false;
	*hwm_kb = value;
	return true;
}

static void *fault_mapping(size_t length)
{
	unsigned char *mapping;
	size_t offset;

	mapping = mmap(NULL, length, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		return MAP_FAILED;
	for (offset = 0; offset < length; offset += PROCESS_PAGE_SIZE)
		mapping[offset] = (unsigned char)offset + 1;
	return mapping;
}

/*
 * VmHWM and ru_maxrss both derive from the mm RSS counters, which are
 * per-CPU batched, so two readings can differ by a few MB of slack.  The
 * unit mismatch this test guards against is a factor of four, so a 4 MB
 * tolerance keeps the check meaningful on a busy system.
 */
static bool close_enough(unsigned long actual, unsigned long expected)
{
	unsigned long difference = actual > expected ? actual - expected :
		expected - actual;

	return difference <= 4096;
}

static void child_workload(int report_fd, size_t length)
{
	struct child_report report;
	void *mapping;

	mapping = fault_mapping(length);
	if (mapping == MAP_FAILED)
		_exit(120);
	/*
	 * VmHWM is an approximate per-CPU reading until an unmap latches the
	 * high-water mark; exit reports that latched value as ru_maxrss.
	 */
	munmap(mapping, length);
	if (!read_hwm(&report.hwm_kb))
		_exit(120);
	report.page_size = sysconf(_SC_PAGESIZE);
	if (write(report_fd, &report, sizeof(report)) != sizeof(report))
		_exit(121);
	close(report_fd);
	_exit(0);
}

static bool collect_child(bool native, size_t length,
			  struct child_report *report)
{
	char fd_string[24];
	char length_string[32];
	int pipefd[2];
	int status;
	pid_t pid;

	if (pipe(pipefd))
		return false;
	pid = fork();
	if (!pid) {
		close(pipefd[0]);
		if (!native)
			child_workload(pipefd[1], length);
		snprintf(fd_string, sizeof(fd_string), "%d", pipefd[1]);
		snprintf(length_string, sizeof(length_string), "%zu", length);
		ppps_execl(false, NULL, "--native-child", fd_string,
			   length_string, NULL);
		_exit(123);
	}
	close(pipefd[1]);
	if (pid < 0 || read(pipefd[0], report, sizeof(*report)) !=
						       sizeof(*report)) {
		close(pipefd[0]);
		return false;
	}
	close(pipefd[0]);
	return waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
		WEXITSTATUS(status) == 0;
}

static int run_test(void)
{
	struct child_report child_4k = { };
	struct child_report child_native = { };
	struct rusage usage;
	unsigned long expected_children;
	unsigned long parent_hwm = 0;
	void *mapping;

	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(7);

	mapping = fault_mapping(PARENT_SIZE);
	ksft_test_result(mapping != MAP_FAILED && read_hwm(&parent_hwm),
			 "fault and measure the parent workload\n");
	if (mapping == MAP_FAILED || !parent_hwm)
		ksft_exit_fail_msg("parent workload setup failed: %s\n",
				   strerror(errno));
	ksft_test_result(!getrusage(RUSAGE_SELF, &usage),
			 "read RUSAGE_SELF\n");
	ksft_print_msg("self VmHWM=%lu ru_maxrss=%ld\n", parent_hwm,
		       usage.ru_maxrss);
	ksft_test_result(close_enough(usage.ru_maxrss, parent_hwm),
			 "report self maxrss in process-page KB\n");
	munmap(mapping, PARENT_SIZE);

	ksft_test_result(collect_child(false, CHILD_4K_SIZE, &child_4k),
			 "collect a 4K child's high-watermark\n");
	ksft_test_result(collect_child(true, CHILD_NATIVE_SIZE, &child_native),
			 "collect a native-page child's high-watermark\n");
	ksft_print_msg("child4k page=%ld hwm=%lu child_native page=%ld hwm=%lu\n",
		       child_4k.page_size, child_4k.hwm_kb,
		       child_native.page_size, child_native.hwm_kb);
	ksft_test_result(!getrusage(RUSAGE_CHILDREN, &usage),
			 "read RUSAGE_CHILDREN\n");
	expected_children = child_4k.hwm_kb > child_native.hwm_kb ?
		child_4k.hwm_kb : child_native.hwm_kb;
	ksft_print_msg("children expected_max=%lu ru_maxrss=%ld\n",
		       expected_children, usage.ru_maxrss);
	ksft_test_result(close_enough(usage.ru_maxrss, expected_children),
			 "compare child maxrss values in common KB units\n");

	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode)
		exec_compat(argv[0], PPPS_RUN_FLAG, NULL);
	if (argc == 2 && !strcmp(mode, PPPS_RUN_FLAG))
		return run_test();
	if (argc == 4 && !strcmp(mode, "--native-child"))
		child_workload(atoi(argv[2]), strtoull(argv[3], NULL, 0));
	return EXIT_FAILURE;
}
