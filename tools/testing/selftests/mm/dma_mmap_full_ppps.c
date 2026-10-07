// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process must be able to mmap() a whole buffer exported with
 * dma_mmap_attrs() / dma_mmap_pages(), not only a quarter of it: the fixture
 * buffer is one native 16K page whose 4K slices hold marker, marker + 1, ...
 * Mapping all four slices, mapping the upper half at offset 8K and the
 * native control mapping must all succeed and show those markers; a mapping
 * longer than the buffer must fail without populating anything.
 *
 * The fixture is selected on the command line: "attrs" or "pages".
 */
#define _GNU_SOURCE

#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define BUFFER_SIZE	NATIVE_PAGE_SIZE

struct fixture {
	const char *name;
	const char *device;
	unsigned char marker;
};

static const struct fixture fixtures[] = {
	{ "attrs", "/dev/dma_mmap_attrs_ppps", 0x61 },
	{ "pages", "/dev/dma_mmap_pages_ppps", 0x51 },
};

static sigjmp_buf fault_env;

static void fault_handler(int sig)
{
	siglongjmp(fault_env, sig);
}

static int probe(const volatile unsigned char *addr, unsigned char *value)
{
	int sig = sigsetjmp(fault_env, 1);

	if (sig)
		return sig;
	*value = *addr;
	return 0;
}

/* Every 4K slice of [map, map + len) holds marker + first_slice + i. */
static bool check_slices(const unsigned char *map, size_t len,
			 unsigned char marker, unsigned int first_slice,
			 const char *what)
{
	size_t off;
	unsigned char value;
	bool ok = true;

	for (off = 0; off < len; off += PROCESS_PAGE_SIZE) {
		unsigned char want = marker + first_slice + off / PROCESS_PAGE_SIZE;
		int sig = probe(map + off, &value);

		if (sig) {
			ksft_print_msg("%s: offset %zu raised signal %d\n", what,
				       off, sig);
			ok = false;
		} else if (value != want) {
			ksft_print_msg("%s: offset %zu reads %#x, expected %#x\n",
				       what, off, value, want);
			ok = false;
		}
	}
	return ok;
}

static bool map_and_check(int fd, size_t len, off_t offset,
			  unsigned char marker, const char *what)
{
	unsigned char *map = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, offset);
	bool ok;

	if (map == MAP_FAILED) {
		ksft_print_msg("%s: mmap(%zu bytes at %lld): %s\n", what, len,
			       (long long)offset, strerror(errno));
		return false;
	}
	ok = check_slices(map, len, marker, offset / PROCESS_PAGE_SIZE, what);
	munmap(map, len);
	return ok;
}

/* A mapping past the end of the buffer fails and leaves no PTEs behind. */
static bool oversized_rejected(int fd, const char *what)
{
	size_t len = BUFFER_SIZE + NATIVE_PAGE_SIZE;
	unsigned char *reservation, *map, value;
	bool ok = true;
	size_t off;

	reservation = mmap(NULL, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS,
			   -1, 0);
	if (reservation == MAP_FAILED)
		return false;
	map = mmap(reservation, len, PROT_READ, MAP_SHARED | MAP_FIXED, fd, 0);
	if (map != MAP_FAILED) {
		ksft_print_msg("%s: a %zu-byte mapping of a %lu-byte buffer succeeded\n",
			       what, len, BUFFER_SIZE);
		munmap(map, len);
		return false;
	}
	/* The failed mmap() unmapped the reservation; nothing may be readable. */
	for (off = 0; off < len; off += PROCESS_PAGE_SIZE) {
		if (!probe(reservation + off, &value)) {
			ksft_print_msg("%s: offset %zu readable after failed mmap\n",
				       what, off);
			ok = false;
		}
	}
	return ok;
}

static int run_case(const struct fixture *fixture)
{
	struct sigaction action = { .sa_handler = fault_handler };
	bool compat = ppps_is_compat_process();
	int fd, ret = KSFT_PASS;

	sigemptyset(&action.sa_mask);
	if (sigaction(SIGSEGV, &action, NULL) || sigaction(SIGBUS, &action, NULL))
		return KSFT_FAIL;
	fd = ppps_open_fixture(fixture->device, O_RDWR);
	if (fd < 0) {
		ksft_print_msg("%s: %s\n", fixture->device, strerror(errno));
		return ppps_fixture_unavailable(errno) ? KSFT_SKIP : KSFT_FAIL;
	}
	if (!map_and_check(fd, BUFFER_SIZE, 0, fixture->marker, "whole buffer"))
		ret = KSFT_FAIL;
	if (compat && !map_and_check(fd, BUFFER_SIZE / 2, BUFFER_SIZE / 2,
				     fixture->marker, "upper half"))
		ret = KSFT_FAIL;
	if (!oversized_rejected(fd, "oversized"))
		ret = KSFT_FAIL;
	close(fd);
	return ret;
}

static const struct fixture *find_fixture(const char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(fixtures); i++)
		if (!strcmp(fixtures[i].name, name))
			return &fixtures[i];
	return NULL;
}

static int run_child(bool compat, const char *name)
{
	int status;
	pid_t pid = fork();

	if (pid < 0)
		ksft_exit_fail_msg("fork: %s\n", strerror(errno));
	if (!pid) {
		ppps_execl(compat, NULL, compat ? "--compat" : "--native", name,
			   NULL);
		_exit(KSFT_SKIP);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return KSFT_FAIL;
	return WEXITSTATUS(status);
}

static void report(int ret, const char *what, const char *name)
{
	if (ret == KSFT_SKIP)
		ksft_test_result_skip("%s (%s)\n", what, name);
	else
		ksft_test_result(ret == KSFT_PASS, "%s (%s)\n", what, name);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);
	const struct fixture *fixture;

	if (mode && argc == 3 && (fixture = find_fixture(argv[2]))) {
		if (!strcmp(mode, "--native"))
			return getpagesize() == (int)NATIVE_PAGE_SIZE ?
			       run_case(fixture) : KSFT_SKIP;
		if (!strcmp(mode, "--compat"))
			return ppps_is_compat_process() ? run_case(fixture) :
							  KSFT_SKIP;
	}
	if (argc != 2 || !(fixture = find_fixture(argv[1]))) {
		fprintf(stderr, "usage: %s attrs|pages\n", argv[0]);
		return KSFT_FAIL;
	}
	ksft_print_header();
	ksft_set_plan(2);
	fflush(stdout);
	report(run_child(false, fixture->name),
	       "native process maps the whole DMA buffer", fixture->name);
	report(run_child(true, fixture->name),
	       "4K compat process maps the whole DMA buffer and its upper half",
	       fixture->name);
	ksft_finished();
}
