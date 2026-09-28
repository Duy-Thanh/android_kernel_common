// SPDX-License-Identifier: GPL-2.0
/* Exact byte-range GUP results, including short success and duplicate pins. */
#define _GNU_SOURCE
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include "kselftest_ppps.h"
#include "iov_iter_ppps.h"
#include "page_range_ppps.h"

static void check_range(int fd, unsigned char *base, size_t page_size,
			size_t backing_offset, const char *name, bool pin)
{
	struct page_range_ppps_args req = {
		.address = (uintptr_t)base + 17,
		.length = page_size,
		.capacity = PAGE_RANGE_PPPS_MAX,
		.pin = pin,
	};
	int ret = ioctl(fd, PAGE_RANGE_PPPS_IOCTL, &req);

	ksft_test_result(
		!ret && req.helpers_ok && req.result == 2 &&
			req.spans[0].offset == backing_offset + 17 &&
			req.spans[0].length == page_size - 17 &&
			req.spans[0].first == 0x31 &&
			req.spans[0].last == 0x31 &&
			req.spans[1].offset == (backing_offset + page_size) %
						       NATIVE_PAGE_SIZE &&
			req.spans[1].length == 17 &&
			req.spans[1].first == 0x72 && req.spans[1].last == 0x72,
		"%s %s exact byte spans and helper units\n",
		pin ? "pin" : "get", name);
}

/* Driver-owned RAM has no page-cache rmap: its fallback is conservative. */
static bool driver_stats(unsigned char *map, size_t page_size,
			 unsigned long expected_pss, bool exclusive)
{
	char *line = NULL;
	size_t cap = 0;
	unsigned long lo, hi, pss = ULONG_MAX;
	bool selected = false, ok = true;
	FILE *file = fopen("/proc/self/smaps", "re");
	uint64_t entry = 0;
	int fd, i;

	if (!file)
		return false;
	while (getline(&line, &cap, file) > 0) {
		if (sscanf(line, "%lx-%lx", &lo, &hi) == 2)
			selected = (uintptr_t)map >= lo && (uintptr_t)map < hi;
		else if (selected && sscanf(line, "Pss: %lu kB", &pss) == 1)
			break;
	}
	free(line);
	fclose(file);
	fd = open("/proc/self/pagemap", O_RDONLY);
	if (fd < 0)
		return false;
	for (i = 0; i < 4; i++) {
		ok &= pread(fd, &entry, sizeof(entry),
			    ((uintptr_t)map / page_size + i) * 8) == sizeof(entry);
		ok &= !!(entry & (1ULL << 63)) &&
		      !!(entry & (1ULL << 56)) == exclusive;
	}
	close(fd);
	return ok && pss == expected_pss;
}

static void check_driver_stats(unsigned char *map, size_t page_size)
{
	bool compat = page_size == PROCESS_PAGE_SIZE;
	int barrier[2], status;
	unsigned long pss = compat ? 4 : 4 * page_size / 1024;
	pid_t pid;
	char byte;

	ksft_test_result(driver_stats(map, page_size, pss, !compat),
			 "driver RAM uses per-page mapcount, not false exclusivity\n");
	if (pipe(barrier))
		ksft_exit_fail_msg("pipe failed\n");
	pid = fork();
	if (!pid) {
		close(barrier[1]);
		_exit(read(barrier[0], &byte, 1) != 1);
	}
	if (pid < 0)
		ksft_exit_fail_msg("fork failed\n");
	close(barrier[0]);
	/* VM_MIXEDMAP page tables are copied before fork returns. */
	ksft_test_result(driver_stats(map, page_size, pss / 2, false),
			 "shared driver RAM is never reported exclusive\n");
	if (write(barrier[1], "x", 1) != 1 || waitpid(pid, &status, 0) != pid ||
	    !WIFEXITED(status) || WEXITSTATUS(status))
		ksft_exit_fail_msg("child synchronization failed\n");
	close(barrier[1]);
	ksft_test_result(driver_stats(map, page_size, pss, !compat),
			 "driver accounting recovers after child exit\n");
}

static void check_driver_mapping(int fd, size_t page_size)
{
	static const unsigned int slices[] = { 3, 1, 0, 2 };
	static const unsigned char values[] = { 0x31, 0x72, 0x93, 0xb4 };
	bool compat = page_size == PROCESS_PAGE_SIZE;
	unsigned char *mapping, *copy;
	struct page_range_ppps_args req = {};
	struct iovec local, remote;
	size_t size = 4 * page_size;
	int i, pin, mem;

	mapping = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	copy = malloc(size);
	if (mapping == MAP_FAILED || !copy)
		ksft_exit_fail_msg("driver slice fixture setup failed: %s\n", strerror(errno));
	for (pin = 0; pin <= 1; pin++) {
		bool ok;

		req.address = (uintptr_t)mapping;
		req.length = size;
		req.capacity = 4;
		req.pin = pin;
		ok = !ioctl(fd, PAGE_RANGE_PPPS_IOCTL, &req) && req.result == 4;
		for (i = 0; i < 4; i++) {
			unsigned int idx = compat ? slices[i] : i;

			ok &= req.spans[i].offset == (compat ? idx * page_size : 0) &&
				req.spans[i].length == page_size &&
				req.spans[i].first == values[idx] &&
				req.spans[i].last == values[idx];
		}
		ksft_test_result(ok, "%s follows driver-selected physical slices\n",
				 pin ? "pin" : "get");
	}
	local = (struct iovec){ copy, size };
	remote = (struct iovec){ mapping, size };
	ksft_test_result(process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == size &&
			 !memcmp(copy, mapping, size),
			 "process_vm_readv preserves driver slice order\n");
	for (i = 0; i < 4; i++)
		memset(copy + i * page_size, 0xc0 + i, page_size);
	ksft_test_result(process_vm_writev(getpid(), &local, 1, &remote, 1, 0) == size &&
			 !memcmp(copy, mapping, size),
			 "process_vm_writev preserves driver slice order\n");
	mem = open("/proc/self/mem", O_RDWR);
	if (mem < 0)
		ksft_exit_fail_msg("open self mem failed\n");
	ksft_test_result(pread(mem, copy, size, (uintptr_t)mapping) == size &&
			 !memcmp(copy, mapping, size),
			 "self mem read preserves driver slice order\n");
	for (i = 0; i < 4; i++)
		memset(copy + i * page_size, 0xd0 + i, page_size);
	ksft_test_result(pwrite(mem, copy, size, (uintptr_t)mapping) == size &&
			 !memcmp(copy, mapping, size),
			 "self mem write preserves driver slice order\n");
	close(mem);
	check_driver_stats(mapping, page_size);
	free(copy);
	munmap(mapping, size);
}

static int run_range_test(void)
{
	size_t page_size = sysconf(_SC_PAGESIZE);
	struct page_range_ppps_args req = {};
	unsigned char *anon, *file;
	int fd, memfd, pin;

	ksft_print_header();
	ksft_set_plan(18);
	fd = ppps_open_fixture_or_skip("/dev/" IOV_ITER_PPPS_DEVICE_NAME,
				       O_RDWR);
	memfd = memfd_create("page-range", 0);
	if (memfd < 0 || ftruncate(memfd, 3 * NATIVE_PAGE_SIZE))
		ksft_exit_fail_msg("memfd setup failed\n");
	anon = mmap(NULL, 3 * NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	file = mmap(NULL, 2 * NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		    MAP_SHARED, memfd, page_size);
	if (anon == MAP_FAILED || file == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed\n");
	memset(anon, 0x31, page_size);
	memset(anon + page_size, 0x72, page_size);
	memset(file, 0x31, page_size);
	memset(file + page_size, 0x72, page_size);
	for (pin = 0; pin <= 1; pin++) {
		check_range(fd, anon, page_size, 0, "anonymous", pin);
		check_range(fd, file, page_size, page_size % NATIVE_PAGE_SIZE,
			    "file with nonzero offset", pin);
	}
	check_driver_mapping(fd, page_size);
	req.address = (uintptr_t)anon + 17;
	req.length = page_size;
	req.capacity = 1;
	req.pin = 1;
	ksft_test_result(!ioctl(fd, PAGE_RANGE_PPPS_IOCTL, &req) &&
				 req.result == -ENOSPC,
			 "insufficient capacity owns no pins\n");
	req.length = 0;
	req.capacity = 0;
	ksft_test_result(!ioctl(fd, PAGE_RANGE_PPPS_IOCTL, &req) &&
				 req.result == 0,
			 "empty byte range needs no output slots\n");
	req.address = UINT64_MAX - 31;
	req.length = 64;
	req.capacity = 2;
	/* Tag stripping may turn the wrap into an inaccessible address. */
	ksft_test_result(!ioctl(fd, PAGE_RANGE_PPPS_IOCTL, &req) &&
				 req.result < 0,
			 "invalid high byte range is rejected\n");
	if (mprotect(anon + page_size, page_size, PROT_NONE))
		ksft_exit_fail_msg("mprotect failed\n");
	for (pin = 0; pin <= 1; pin++) {
		req.address = (uintptr_t)anon + 17;
		req.length = page_size;
		req.capacity = 2;
		req.pin = pin;
		ksft_test_result(
			!ioctl(fd, PAGE_RANGE_PPPS_IOCTL, &req) &&
				req.result == 1 &&
				req.spans[0].length == page_size - 17 &&
				req.spans[0].offset == 17,
			"%s short result describes only accessible bytes\n",
			pin ? "pin" : "get");
	}
	munmap(anon, 3 * NATIVE_PAGE_SIZE);
	munmap(file, 2 * NATIVE_PAGE_SIZE);
	close(memfd);
	close(fd);
	ksft_finished();
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--native")) {
		if (sysconf(_SC_PAGESIZE) != NATIVE_PAGE_SIZE)
			exec_native(argv[0], "--native", NULL);
		return run_range_test();
	}
	return ppps_compat_main(argc, argv, run_range_test);
}
