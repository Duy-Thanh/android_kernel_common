// SPDX-License-Identifier: GPL-2.0
/*
 * After MADV_DONTNEED a private anonymous 4K page reads back as zeroes, even
 * when the 16K native folio backing its window stays alive for the other
 * three slices.  That must also hold when the page is next faulted in by a
 * remote access (process_vm_writev(), /proc/<pid>/mem, ptrace) rather than
 * by the process itself: the bytes the remote writer did not write must be
 * zero, not the contents the slice had before MADV_DONTNEED.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/uio.h>

#include "ppps_tuple_test.h"

#define OLD_BYTE	0xc3
#define NEW_BYTE	0x5e
#define WRITE_OFFSET	100

typedef bool (*remote_write_fn)(void *address, unsigned char byte);

static bool write_vm_writev(void *address, unsigned char byte)
{
	struct iovec local = { .iov_base = &byte, .iov_len = 1 };
	struct iovec remote = { .iov_base = address, .iov_len = 1 };

	return process_vm_writev(getpid(), &local, 1, &remote, 1, 0) == 1;
}

static bool write_proc_mem(void *address, unsigned char byte)
{
	int fd = open("/proc/self/mem", O_RDWR | O_CLOEXEC);
	bool ok;

	if (fd < 0)
		return false;
	ok = pwrite(fd, &byte, 1, (off_t)(uintptr_t)address) == 1;
	close(fd);
	return ok;
}

static bool check_slice(const unsigned char *slice, const char *how)
{
	unsigned long i, stale = 0;

	if (slice[WRITE_OFFSET] != NEW_BYTE) {
		ksft_print_msg("%s: written byte reads %#x\n", how,
			       slice[WRITE_OFFSET]);
		return false;
	}
	for (i = 0; i < PROCESS_PAGE_SIZE; i++)
		if (i != WRITE_OFFSET && slice[i])
			stale++;
	if (stale) {
		ksft_print_msg("%s: %lu bytes of the DONTNEED'd slice are not zero (first byte %#x)\n",
			       how, stale, slice[0]);
		return false;
	}
	return true;
}

static int run_case(remote_write_fn write_fn, const char *how,
		    unsigned int slice)
{
	unsigned char *reservation, *base, *target;
	unsigned int i;
	bool ok = true;

	base = map_aligned(NATIVE_PAGE_SIZE, &reservation);
	if (base == MAP_FAILED)
		ksft_exit_fail_msg("mmap: %s\n", strerror(errno));
	memset(base, OLD_BYTE, NATIVE_PAGE_SIZE);
	target = base + slice * PROCESS_PAGE_SIZE;

	if (madvise(target, PROCESS_PAGE_SIZE, MADV_DONTNEED))
		ksft_exit_fail_msg("MADV_DONTNEED: %s\n", strerror(errno));
	if (ppps_page_present(target))
		ksft_print_msg("%s: slice %u still present after MADV_DONTNEED\n",
			       how, slice);

	if (!write_fn(target + WRITE_OFFSET, NEW_BYTE)) {
		ksft_print_msg("%s: remote write failed: %s\n", how,
			       strerror(errno));
		ok = false;
	} else {
		ok = check_slice(target, how);
	}
	for (i = 0; i < PPPS_SLICES; i++) {
		if (i == slice)
			continue;
		if (!all_bytes_are(base + i * PROCESS_PAGE_SIZE,
				   PROCESS_PAGE_SIZE, OLD_BYTE)) {
			ksft_print_msg("%s: sibling slice %u changed\n", how, i);
			ok = false;
		}
	}
	munmap(reservation, NATIVE_PAGE_SIZE * 2);
	return ok;
}

static int run_test(void)
{
	ksft_print_header();
	ksft_set_plan(3);
	ksft_test_result(run_case(write_vm_writev, "process_vm_writev", 1),
			 "process_vm_writev into a DONTNEED'd slice sees zeroes\n");
	ksft_test_result(run_case(write_proc_mem, "/proc/self/mem", 2),
			 "/proc/self/mem write into a DONTNEED'd slice sees zeroes\n");
	ksft_test_result(run_case(write_vm_writev, "process_vm_writev slice 0", 0),
			 "process_vm_writev into DONTNEED'd slice 0 sees zeroes\n");
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
