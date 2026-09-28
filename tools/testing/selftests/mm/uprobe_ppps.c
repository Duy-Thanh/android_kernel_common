// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process probes text mapped one 4K slice past native alignment:
 * the sliced uprobe registers, bumps its ref counter, delivers an unmatched
 * trap from an execute-only VMA, runs from a fallback XOL area and traces.
 */
#define _GNU_SOURCE

#include <limits.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define XOL_RESERVE_SIZE	(2 * NATIVE_PAGE_SIZE)
#define TARGET_OFFSET	(2 * PROCESS_PAGE_SIZE)
#define REF_CTR_OFFSET	(3 * PROCESS_PAGE_SIZE)
#define TARGET_MAP_ADDR	((void *)0x20001000UL)
#define REF_MAP_ADDR	((void *)0x30001000UL)
#define XOL_HINT_39	((void *)((1UL << 39) - XOL_RESERVE_SIZE))
#define XOL_HINT_47	((void *)((1UL << 47) - XOL_RESERVE_SIZE))
#define TARGET_PATH	"/tmp/uprobe-ppps-target"
#define TRACE_ROOT	"/sys/kernel/tracing"
#define UPROBE_BRK_INSN	0xd42000a0U
#define RET_INSN	0xd65f03c0U

typedef unsigned long (*target_fn_t)(unsigned long);

static bool write_text(const char *path, const char *text, int flags)
{
	ssize_t length = strlen(text);
	ssize_t written;
	int fd;

	fd = open(path, O_WRONLY | O_CLOEXEC | flags);
	if (fd < 0)
		return false;
	written = write(fd, text, length);
	close(fd);
	return written == length;
}

static void trap_child_exit(int signal)
{
	_exit(signal == SIGTRAP ? EXIT_SUCCESS : EXIT_FAILURE);
}

static bool unmatched_xom_trap_is_delivered(void *mapping)
{
	struct sigaction action = {
		.sa_handler = trap_child_exit,
	};
	int status;
	pid_t pid;

	pid = fork();
	if (pid < 0)
		return false;
	if (!pid) {
		sigemptyset(&action.sa_mask);
		sigaction(SIGTRAP, &action, NULL);
		sigaction(SIGALRM, &action, NULL);
		if (mprotect(mapping, PROCESS_PAGE_SIZE, PROT_EXEC))
			_exit(EXIT_FAILURE);
		alarm(2);
		((void (*)(void))mapping)();
		_exit(EXIT_FAILURE);
	}

	do {
		pid = waitpid(pid, &status, 0);
	} while (pid < 0 && errno == EINTR);

	return pid > 0 && WIFEXITED(status) &&
		WEXITSTATUS(status) == EXIT_SUCCESS;
}

static void *map_target(void **ref_mapping)
{
#ifdef __aarch64__
	const uint32_t instructions[] = {
		0x91000400, /* add x0, x0, #1 */
		RET_INSN, /* ret */
	};
	const uint32_t unmatched_trap = UPROBE_BRK_INSN;
	const uint32_t wrong_slice = RET_INSN;
	void *mapping = MAP_FAILED;
	int fd;

	*ref_mapping = MAP_FAILED;
	fd = open(TARGET_PATH, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0700);
	if (fd < 0)
		return MAP_FAILED;
	if (ftruncate(fd, NATIVE_PAGE_SIZE) ||
	    pwrite(fd, &unmatched_trap, sizeof(unmatched_trap), 0) !=
		    sizeof(unmatched_trap) ||
	    pwrite(fd, &wrong_slice, sizeof(wrong_slice), PROCESS_PAGE_SIZE) !=
		    sizeof(wrong_slice) ||
	    pwrite(fd, instructions, sizeof(instructions), TARGET_OFFSET) !=
		    sizeof(instructions))
		goto out;
	mapping = mmap(TARGET_MAP_ADDR, NATIVE_PAGE_SIZE, PROT_READ | PROT_EXEC,
		       MAP_PRIVATE | MAP_FIXED_NOREPLACE, fd, 0);
	if (mapping == MAP_FAILED)
		goto out;
	*ref_mapping = mmap(REF_MAP_ADDR, NATIVE_PAGE_SIZE,
			    PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_FIXED_NOREPLACE, fd, 0);
	if (*ref_mapping == MAP_FAILED) {
		munmap(mapping, NATIVE_PAGE_SIZE);
		mapping = MAP_FAILED;
	}
out:
	close(fd);
	return mapping;
#else
	return MAP_FAILED;
#endif
}

static bool trace_has_event(void)
{
	char buffer[65536];
	ssize_t size;
	int fd;

	fd = open(TRACE_ROOT "/trace", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	size = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	if (size < 0)
		return false;
	buffer[size] = '\0';
	return strstr(buffer, "uprobe_ppps:");
}

static int run_test(void)
{
#ifdef __aarch64__
	const char *delete_command = "-:ppps/uprobe_ppps\n";
	const char *create_command =
		"p:ppps/uprobe_ppps " TARGET_PATH ":0x2000(0x3000)\n";
	uint16_t *ref_ctr;
	target_fn_t target;
	void *ref_mapping;
	void *mapping;
	void *xol_hint_39;
	void *xol_hint_47;
	bool hint_blocked;
	bool registered;
	bool unregistered;
	bool observed;
	unsigned int i;

	ksft_print_header();
	ksft_set_plan(10);

	mapping = map_target(&ref_mapping);
	ksft_test_result(mapping == TARGET_MAP_ADDR &&
			 ref_mapping == REF_MAP_ADDR,
			 "map text and ref-counter VMAs one 4K slice past native alignment\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("target mapping failed: %s\n",
				   strerror(errno));
	target = (target_fn_t)((char *)mapping + TARGET_OFFSET);
	ref_ctr = (uint16_t *)((char *)ref_mapping + REF_CTR_OFFSET);
	ksft_print_msg("mapping=%p target=%p native_slice=%lu\n", mapping,
		       target, TARGET_OFFSET / PROCESS_PAGE_SIZE);

	for (i = 0; i < 1024; i++)
		target(i);
	ksft_test_result(true, "warm the target instruction and translation\n");

	xol_hint_39 = mmap(XOL_HINT_39, XOL_RESERVE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
			   -1, 0);
	xol_hint_47 = mmap(XOL_HINT_47, XOL_RESERVE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
			   -1, 0);
	hint_blocked = xol_hint_39 == XOL_HINT_39 ||
		xol_hint_47 == XOL_HINT_47;
	ksft_test_result(hint_blocked,
			 "occupy the architecture's preferred XOL address\n");
	if (!hint_blocked)
		ksft_exit_fail_msg("failed to occupy either XOL hint: %s\n",
				   strerror(errno));

	write_text(TRACE_ROOT "/uprobe_events", delete_command, O_APPEND);
	write_text(TRACE_ROOT "/trace", "", O_TRUNC);
	registered = write_text(TRACE_ROOT "/uprobe_events", create_command,
				O_APPEND) &&
		write_text(TRACE_ROOT "/events/ppps/uprobe_ppps/enable", "1", 0) &&
		write_text(TRACE_ROOT "/tracing_on", "1", 0);
	ksft_test_result(registered, "register and enable the sliced uprobe\n");
	if (!registered)
		ksft_exit_fail_msg("uprobe registration failed: %s\n",
				   strerror(errno));
	ksft_test_result(*ref_ctr == 1,
			 "increment the sliced userspace reference counter\n");
	ksft_test_result(unmatched_xom_trap_is_delivered(mapping),
			 "deliver an unmatched trap from a sliced execute-only VMA\n");

	ksft_test_result(target(41) == 42,
			 "execute the probed instruction from the fallback XOL area\n");
	write_text(TRACE_ROOT "/events/ppps/uprobe_ppps/enable", "0", 0);
	observed = trace_has_event();
	ksft_test_result(observed, "record an event from the sliced uprobe\n");
	unregistered = write_text(TRACE_ROOT "/uprobe_events", delete_command,
				  O_APPEND);
	ksft_test_result(unregistered, "unregister the sliced uprobe\n");
	ksft_test_result(*ref_ctr == 0,
			 "decrement the sliced userspace reference counter\n");

	munmap(mapping, NATIVE_PAGE_SIZE);
	munmap(ref_mapping, NATIVE_PAGE_SIZE);
	if (xol_hint_39 != MAP_FAILED)
		munmap(xol_hint_39, XOL_RESERVE_SIZE);
	if (xol_hint_47 != MAP_FAILED)
		munmap(xol_hint_47, XOL_RESERVE_SIZE);
	unlink(TARGET_PATH);
	ksft_finished();
#else
	ksft_exit_skip("PPPS uprobes are arm64-specific\n");
#endif
}

PPPS_COMPAT_MAIN(run_test)
