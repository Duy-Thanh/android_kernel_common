/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared helpers for the PPPS (per-process page size) selftests.
 *
 * A PPPS kernel is a native 16K arm64 kernel on which a process carrying the
 * ADDR_4KB_COMPAT_PAGE_SIZE personality bit sees 4K pages.  Nearly every test
 * in this corpus starts as whatever the runner is, re-execs itself as a 4K
 * compat process and only then runs its checks; a few also spawn a native
 * (16K) helper.  This header holds the one copy of that dance plus the small
 * /proc readers the tests share.  Everything is static inline so each test
 * remains a single translation unit.
 *
 * The 4K compat interface is deliberately minimal: whatever a test observes
 * that is not part of that interface is expected to behave as it does for a
 * native 16K process.
 */
#ifndef __KSELFTEST_PPPS_H
#define __KSELFTEST_PPPS_H

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/personality.h>
#include <sys/types.h>

#include "kselftest.h"

/*
 * personality(2) bit selecting the 4K compat page size at the next exec.
 * The uapi <linux/personality.h> carries it as an enumerator, which the libc
 * <sys/personality.h> does not re-export, hence the macro.
 */
#define ADDR_4KB_COMPAT_PAGE_SIZE	0x40000000

#ifndef AF_XDP
#define AF_XDP 44
#endif
#ifndef SOL_XDP
#define SOL_XDP 283
#endif

/* Page size of a 4K compat process and of the native kernel. */
#define PROCESS_PAGE_SIZE		4096UL
#define NATIVE_PAGE_SIZE		16384UL
/* Number of 4K process pages ("slices") backed by one native page. */
#define PPPS_SLICES			(NATIVE_PAGE_SIZE / PROCESS_PAGE_SIZE)

/* Mode flag the standard main passes to its 4K compat re-exec. */
#define PPPS_RUN_FLAG			"--run"

/*
 * argv[0] of the running test, recorded by ppps_compat_main() and
 * ppps_run_mode() so helpers that fork and re-exec deep inside the test do
 * not need argv threaded through to them.
 */
static const char *ppps_argv0 __attribute__((unused)) = "/proc/self/exe";

static inline bool ppps_is_compat_process(void)
{
	return getpagesize() == (int)PROCESS_PAGE_SIZE;
}

/* Hard precondition of every compat-side test body. */
static inline void ppps_require_compat(void)
{
	if (!ppps_is_compat_process())
		ksft_exit_fail_msg("process page size is %d, expected a %lu-byte compat process\n",
				   getpagesize(), PROCESS_PAGE_SIZE);
}

/* Re-exec /proc/self/exe with the compat bit set or cleared.  -1 with errno. */
static inline int ppps_execv(bool compat, char *const argv[])
{
	int persona = personality(0xffffffffUL);

	if (persona < 0)
		return -1;
	persona = compat ? persona | ADDR_4KB_COMPAT_PAGE_SIZE :
			   persona & ~ADDR_4KB_COMPAT_PAGE_SIZE;
	if (personality(persona) < 0)
		return -1;
	return execv("/proc/self/exe", argv);
}

#define PPPS_MAX_EXEC_ARGS 15

/* Collect the NULL-terminated list after @argv0 into @argv. */
static inline void ppps_build_argv(const char *argv[PPPS_MAX_EXEC_ARGS + 1],
				   const char *argv0, va_list ap)
{
	unsigned int i = 1;

	argv[0] = argv0 ? argv0 : ppps_argv0;
	while (i < PPPS_MAX_EXEC_ARGS &&
	       (argv[i] = va_arg(ap, const char *)))
		i++;
	argv[i] = NULL;
}

/*
 * execl()-style re-exec that returns -1 (with errno) on failure, for forked
 * helpers that must fail quietly rather than bail out of the TAP stream.
 * @argv0 may be NULL to reuse the recorded argv[0].
 */
static inline int __attribute__((sentinel))
ppps_execl(bool compat, const char *argv0, ...)
{
	const char *argv[PPPS_MAX_EXEC_ARGS + 1];
	va_list ap;

	va_start(ap, argv0);
	ppps_build_argv(argv, argv0, ap);
	va_end(ap);
	return ppps_execv(compat, (char *const *)argv);
}

static inline void __noreturn ppps_vexec_or_die(bool compat, const char *argv0,
						 va_list ap)
{
	const char *argv[PPPS_MAX_EXEC_ARGS + 1];

	ppps_build_argv(argv, argv0, ap);
	ppps_execv(compat, (char *const *)argv);
	ksft_exit_fail_msg("re-exec as a %s process failed: %s\n",
			   compat ? "4K compat" : "native", strerror(errno));
}

/*
 * Re-exec ourselves as a 4K compat process.  The NULL-terminated argument
 * list follows @argv0, as for execl(); @argv0 may be NULL to reuse the
 * recorded argv[0].  Never returns.
 */
static inline void __noreturn __attribute__((sentinel))
exec_compat(const char *argv0, ...)
{
	va_list ap;

	va_start(ap, argv0);
	ppps_vexec_or_die(true, argv0, ap);
}

/* Re-exec ourselves as a native (16K) process.  Never returns. */
static inline void __noreturn __attribute__((sentinel))
exec_native(const char *argv0, ...)
{
	va_list ap;

	va_start(ap, argv0);
	ppps_vexec_or_die(false, argv0, ap);
}

/*
 * The standard main of a single-body PPPS test: run @run as a 4K compat
 * process, re-execing with PPPS_RUN_FLAG first when we are not one yet.
 * Any other command line is a usage error.
 */
static inline int ppps_compat_main(int argc, char **argv, int (*run)(void))
{
	ppps_argv0 = argv[0];
	if (argc == 2 && !strcmp(argv[1], PPPS_RUN_FLAG)) {
		ppps_require_compat();
		return run();
	}
	if (argc != 1)
		return EXIT_FAILURE;
	if (!ppps_is_compat_process())
		exec_compat(argv[0], PPPS_RUN_FLAG, NULL);
	return run();
}

#define PPPS_COMPAT_MAIN(run_fn)				\
int main(int argc, char **argv)					\
{								\
	return ppps_compat_main(argc, argv, run_fn);		\
}

/*
 * Opt-in entry point for tests with the same assertions in both geometries.
 * Default to compat; --native selects the 16K control.  Always prove native
 * 16K exec first: getpagesize() == 4096 alone also matches a native 4K kernel.
 * Existing single-mode tests keep their current entry point.
 */
static inline void __noreturn
ppps_geometry_exec(bool compat, const char *stage, const char *mode)
{
	int error;

	ppps_execl(compat, NULL, stage, mode, NULL);
	error = errno;
	ksft_print_header();
	if (error == EINVAL || error == EPERM)
		ksft_exit_skip("PPPS personality/exec unavailable: %s\n",
			       strerror(error));
	ksft_exit_fail_msg("PPPS re-exec failed: %s\n", strerror(error));
}

static inline int ppps_geometry_main(int argc, char **argv, int (*run)(void))
{
	ppps_argv0 = argv[0];
#ifndef __aarch64__
	ksft_print_header();
	ksft_exit_skip("requires arm64 16K native / 4K compat geometry\n");
#else
	if (argc == 1 || (argc == 2 && !strcmp(argv[1], "--native")))
		ppps_geometry_exec(false, "--ppps-native",
				   argc == 1 ? "compat" : "native");
	if (argc == 3 && !strcmp(argv[1], "--ppps-native")) {
		if (getpagesize() != NATIVE_PAGE_SIZE) {
			ksft_print_header();
			ksft_exit_skip("native exec does not use 16K pages\n");
		}
		if (!strcmp(argv[2], "native"))
			return run();
		if (!strcmp(argv[2], "compat"))
			ppps_geometry_exec(true, "--ppps-compat", NULL);
	}
	if (argc == 2 && !strcmp(argv[1], "--ppps-compat")) {
		int persona = personality(0xffffffffUL);

		if (getpagesize() != PROCESS_PAGE_SIZE || persona < 0 ||
		    !(persona & ADDR_4KB_COMPAT_PAGE_SIZE)) {
			ksft_print_header();
			ksft_exit_skip("4K compat exec is unavailable\n");
		}
		return run();
	}
	ksft_print_header();
	ksft_exit_fail_msg("usage: %s [--native]\n", argv[0]);
#endif
}

/*
 * The mode of a multi-role test: the first "--flag" argument, or, when
 * @envvar is given and no flag is present, the value of that environment
 * variable.  NULL means a top-level invocation.  Records argv[0].
 */
static inline const char *ppps_run_mode(int argc, char **argv,
					const char *envvar)
{
	if (argc > 0 && argv)
		ppps_argv0 = argv[0];
	if (argc >= 2 && !strncmp(argv[1], "--", 2))
		return argv[1];
	return envvar ? getenv(envvar) : NULL;
}

/* Whole-buffer I/O, retrying short transfers and EINTR. */

static inline bool read_full(int fd, void *buffer, size_t size)
{
	unsigned char *position = buffer;

	while (size) {
		ssize_t bytes = read(fd, position, size);

		if (bytes < 0 && errno == EINTR)
			continue;
		if (bytes <= 0)
			return false;
		position += bytes;
		size -= bytes;
	}
	return true;
}

static inline bool write_full(int fd, const void *buffer, size_t size)
{
	const unsigned char *position = buffer;

	while (size) {
		ssize_t bytes = write(fd, position, size);

		if (bytes < 0 && errno == EINTR)
			continue;
		if (bytes <= 0)
			return false;
		position += bytes;
		size -= bytes;
	}
	return true;
}

static inline bool pwrite_full(int fd, const void *buffer, size_t size,
			       off_t offset)
{
	const unsigned char *position = buffer;

	while (size) {
		ssize_t bytes = pwrite(fd, position, size, offset);

		if (bytes < 0 && errno == EINTR)
			continue;
		if (bytes <= 0)
			return false;
		position += bytes;
		offset += bytes;
		size -= bytes;
	}
	return true;
}

/* /proc/<pid>/pagemap */

#define PAGEMAP_PRESENT		UINT64_C(0x8000000000000000)
#define PAGEMAP_SWAPPED		UINT64_C(0x4000000000000000)
#define PAGEMAP_FILE_SHARED	UINT64_C(0x2000000000000000)
#define PAGEMAP_UFFD_WP		UINT64_C(0x0200000000000000)
#define PAGEMAP_EXCLUSIVE	UINT64_C(0x0100000000000000)
#define PAGEMAP_SOFT_DIRTY	UINT64_C(0x0080000000000000)
#define PAGEMAP_PFN_MASK	((UINT64_C(1) << 55) - 1)

/* Path of a /proc file of @pid; 0 means this process. */
static inline void ppps_proc_path(char *path, size_t size, pid_t pid,
				  const char *file)
{
	if (pid)
		snprintf(path, size, "/proc/%d/%s", (int)pid, file);
	else
		snprintf(path, size, "/proc/self/%s", file);
}

/*
 * The pagemap entry of @address in process @pid (0 = self), whose pages are
 * @page_size bytes: pagemap is indexed in that process's own page units.
 */
static inline bool ppps_pagemap_entry_of(pid_t pid, unsigned long page_size,
					 uintptr_t address, uint64_t *entry)
{
	off_t offset = (off_t)(address / page_size) * sizeof(*entry);
	char path[64];
	int fd;
	bool ok;

	ppps_proc_path(path, sizeof(path), pid, "pagemap");
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	ok = pread(fd, entry, sizeof(*entry), offset) == sizeof(*entry);
	close(fd);
	return ok;
}

/* The pagemap entry of @address in this process. */
static inline bool ppps_pagemap_entry(const void *address, uint64_t *entry)
{
	return ppps_pagemap_entry_of(0, getpagesize(), (uintptr_t)address,
				     entry);
}

/* P3S pagemap reports compat PFNs in 4K units; compare native folios. */
static inline uint64_t ppps_native_pfn(uint64_t pfn)
{
	return getpagesize() == PROCESS_PAGE_SIZE ? pfn / PPPS_SLICES : pfn;
}

/* The native PFN of a present page; false when absent or hidden. */
static inline bool ppps_pfn(const void *address, uint64_t *pfn)
{
	uint64_t entry;

	if (!ppps_pagemap_entry(address, &entry) || !(entry & PAGEMAP_PRESENT))
		return false;
	*pfn = ppps_native_pfn(entry & PAGEMAP_PFN_MASK);
	return *pfn != 0;
}

static inline bool ppps_page_present(const void *address)
{
	uint64_t entry;

	return ppps_pagemap_entry(address, &entry) &&
	       (entry & PAGEMAP_PRESENT);
}

static inline bool ppps_page_swapped(const void *address)
{
	uint64_t entry;

	return ppps_pagemap_entry(address, &entry) &&
	       !(entry & PAGEMAP_PRESENT) && (entry & PAGEMAP_SWAPPED);
}

/* /proc/<pid>/smaps */

/*
 * Sum of the "@field:" line, in bytes, over every VMA of @pid (0 = self)
 * that overlaps [@address, @address + @length).  False when no VMA matched.
 * With @length 1 this is the field of the single VMA containing @address.
 */
static inline bool ppps_smaps_sum(pid_t pid, uintptr_t address, size_t length,
				  const char *field, unsigned long *bytes)
{
	uintptr_t target_end = address + length;
	unsigned long start, end, value_kb;
	char path[64], format[64];
	char *line = NULL;
	size_t capacity = 0;
	bool found = false, in_target = false;
	FILE *smaps;

	*bytes = 0;
	snprintf(format, sizeof(format), "%s: %%lu kB", field);
	ppps_proc_path(path, sizeof(path), pid, "smaps");
	smaps = fopen(path, "re");
	if (!smaps)
		return false;
	while (getline(&line, &capacity, smaps) >= 0) {
		if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
			in_target = address < end && target_end > start;
			continue;
		}
		if (in_target && sscanf(line, format, &value_kb) == 1) {
			*bytes += value_kb * 1024;
			found = true;
			in_target = false;
		}
	}
	free(line);
	fclose(smaps);
	return found;
}

/* The same for a range of this process. */
static inline bool ppps_smaps_bytes(const void *address, size_t length,
				    const char *field, unsigned long *bytes)
{
	return ppps_smaps_sum(0, (uintptr_t)address, length, field, bytes);
}

/* /proc/<pid>/status */

/* The "@field:" line of @pid's status (0 = self) in kB, or -1. */
static inline long ppps_status_kb(pid_t pid, const char *field)
{
	char path[64], line[256];
	size_t name_len = strlen(field);
	long value = -1;
	FILE *status;

	ppps_proc_path(path, sizeof(path), pid, "status");
	status = fopen(path, "re");
	if (!status)
		return -1;
	while (fgets(line, sizeof(line), status)) {
		if (!strncmp(line, field, name_len) && line[name_len] == ':') {
			if (sscanf(line + name_len + 1, " %ld kB", &value) != 1)
				value = -1;
			break;
		}
	}
	fclose(status);
	return value;
}

/* Test fixtures (device nodes, debugfs files, ...) */

/* An error from opening a fixture that means "not on this system". */
static inline bool ppps_fixture_unavailable(int error)
{
	return error == ENOENT || error == ENXIO || error == ENODEV ||
	       error == ENOTDIR || error == EACCES || error == EPERM;
}

/* open(2) a fixture with O_CLOEXEC; -1 with errno preserved on failure. */
static inline int ppps_open_fixture(const char *path, int flags)
{
	return open(path, flags | O_CLOEXEC);
}

/*
 * open(2) a fixture the test cannot run without.  A missing or inaccessible
 * fixture skips the test; any other failure fails it.
 */
static inline int ppps_open_fixture_or_skip(const char *path, int flags)
{
	int fd = ppps_open_fixture(path, flags);

	if (fd >= 0)
		return fd;
	if (ppps_fixture_unavailable(errno))
		ksft_exit_skip("%s is unavailable: %s\n", path, strerror(errno));
	ksft_exit_fail_msg("open %s failed: %s\n", path, strerror(errno));
}

#endif /* __KSELFTEST_PPPS_H */
