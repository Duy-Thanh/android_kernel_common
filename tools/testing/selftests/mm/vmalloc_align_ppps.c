// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/wait.h>
#include "kselftest_ppps.h"

static bool mapping_has_backing_bytes(const unsigned char *map,
				      size_t size, unsigned char first)
{
	pid_t pid = fork();
	int status;

	if (pid < 0)
		ksft_exit_fail_msg("fork: %s\n", strerror(errno));
	if (!pid) {
		size_t offset;

		for (offset = 0; offset < size; offset++)
			if (map[offset] != first + offset / PROCESS_PAGE_SIZE) {
				dprintf(STDERR_FILENO,
					"first mismatch at %#zx: mapped=%#x expected=%#lx\n",
					offset, map[offset],
					first + offset / PROCESS_PAGE_SIZE);
				_exit(1);
			}
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid)
		ksft_exit_fail_msg("waitpid: %s\n", strerror(errno));
	if (WIFSIGNALED(status))
		ksft_print_msg("mapping access killed child with signal %d\n",
			       WTERMSIG(status));
	return WIFEXITED(status) && !WEXITSTATUS(status);
}

static int run_test(void)
{
	const unsigned char values[] = { 0x41, 0, 0, 0x62 };
	size_t size = getpagesize();
	unsigned int i;
	int fd;

	ksft_print_header();
	ksft_set_plan(5);
	fd = ppps_open_fixture_or_skip("/dev/vmalloc_align_ppps", O_RDWR);
	for (i = 0; i < 4; i++) {
		unsigned char *map, *reservation = MAP_FAILED;
		bool ok;
		size_t j;

		errno = 0;
		if (size == PROCESS_PAGE_SIZE) {
			unsigned char *aligned;

			reservation = mmap(NULL, 3 * NATIVE_PAGE_SIZE, PROT_NONE,
					   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (reservation == MAP_FAILED)
				ksft_exit_fail_msg("reserve: %s\n", strerror(errno));
			aligned = (unsigned char *)
				(((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
				 ~(uintptr_t)(NATIVE_PAGE_SIZE - 1));
			map = mmap(aligned + PROCESS_PAGE_SIZE, size, PROT_READ,
				   MAP_SHARED | MAP_FIXED, fd,
				   (off_t)i * NATIVE_PAGE_SIZE);
		} else {
			map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd,
				   (off_t)i * NATIVE_PAGE_SIZE);
		}
		if (size == PROCESS_PAGE_SIZE) {
			if (map == MAP_FAILED)
				ok = (i == 1 || i == 2) && errno == EINVAL;
			else
				ok = i != 1 && i != 2;
			if (ok && map != MAP_FAILED) {
				unsigned char expected = i == 0 ? 0x41 : 0x62;

				for (j = 0; j < size; j++)
					if (map[j] != expected)
						break;
				if (j != size)
					ksft_print_msg("case %u first mismatch at %#zx: mapped=%#x expected=%#x\n",
						       i, j, map[j], expected);
				ok = j == size;
			}
		} else if (i == 1 || i == 2) {
			ok = map == MAP_FAILED && errno == EINVAL;
		} else {
			ok = map != MAP_FAILED;
			if (ok)
				ok = mapping_has_backing_bytes(map, size, values[i]);
		}
		ksft_test_result(ok, "%s backing case %u (errno=%d)\n",
				 size == PROCESS_PAGE_SIZE ? "map valid or reject invalid compat" :
				 i == 1 || i == 2 ? "reject unaligned" : "map aligned",
				 i, map == MAP_FAILED ? errno : 0);
		if (map != MAP_FAILED)
			munmap(map, size);
		if (reservation != MAP_FAILED)
			munmap(reservation, 3 * NATIVE_PAGE_SIZE);
	}
	{
		unsigned char *map;
		bool ok;

		errno = 0;
		map = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
		ok = map != MAP_FAILED &&
			mapping_has_backing_bytes(map, NATIVE_PAGE_SIZE, 0x41);
		ksft_test_result(ok,
			"map complete 16K vmalloc backing (errno=%d)\n",
			map == MAP_FAILED ? errno : 0);
		if (map != MAP_FAILED)
			munmap(map, NATIVE_PAGE_SIZE);
	}
	close(fd);
	ksft_finished();
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--native")) {
		if (getpagesize() != NATIVE_PAGE_SIZE)
			exec_native(argv[0], "--native", NULL);
		return run_test();
	}
	return ppps_compat_main(argc, argv, run_test);
}
