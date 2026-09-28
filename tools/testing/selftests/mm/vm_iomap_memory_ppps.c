// SPDX-License-Identifier: GPL-2.0
/*
 * vm_iomap_memory() through the vm_iomap_memory_ppps fixture maps single 4K
 * slices for a 4K compat process: adjacent guard slices stay unmapped, a
 * private PFN mapping COWs a complete 4K page, a 4K file offset selects the
 * matching slice, and a mapping starting at the buffer end is rejected.
 */
#define _GNU_SOURCE

#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"

#define PRIVATE_RESERVATION_SIZE (3 * NATIVE_PAGE_SIZE)
#define FIRST_MARKER	0x71
#define COW_MARKER	0xa5
#define COW_OFFSET	37

static sigjmp_buf fault_environment;

static void fault_handler(int signal_number)
{
	siglongjmp(fault_environment, signal_number);
}

static bool read_byte(const unsigned char *address, unsigned char *value)
{
	if (sigsetjmp(fault_environment, 1))
		return false;
	*value = *address;
	return true;
}

static bool mapping_has_values(const unsigned char *mapping,
			       unsigned long changed_offset)
{
	unsigned long offset;

	for (offset = 0; offset < PROCESS_PAGE_SIZE; offset++) {
		unsigned char expected = offset == changed_offset ?
			COW_MARKER : FIRST_MARKER;

		if (mapping[offset] != expected) {
			ksft_print_msg("offset %lu contains %#x instead of %#x\n",
				       offset, mapping[offset], expected);
			return false;
		}
	}
	return true;
}

static int run_test(void)
{
	struct sigaction action = {
		.sa_handler = fault_handler,
	};
	unsigned char *reservation;
	unsigned char *mapping;
	unsigned char value = 0;
	uintptr_t target_address;
	bool guards_fault = true;
	bool mapping_ok;
	unsigned int guard;
	void *past_end;
	int fd, unaligned_fd;

	ksft_print_header();
	ksft_set_plan(14);

	fd = ppps_open_fixture_or_skip("/dev/vm_iomap_memory_ppps", O_RDWR);
	ksft_test_result(fd >= 0, "open the vm_iomap_memory test device\n");

	reservation = mmap(NULL, NATIVE_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ksft_test_result(reservation != MAP_FAILED,
			 "reserve one native 16K range as PROT_NONE\n");
	if (reservation == MAP_FAILED)
		ksft_exit_fail_msg("guard reservation failed: %s\n",
				   strerror(errno));

	mapping = mmap(reservation, PROCESS_PAGE_SIZE, PROT_READ,
		       MAP_SHARED | MAP_FIXED, fd, 0);
	ksft_test_result(mapping == reservation,
			 "map only the first 4K slice with vm_iomap_memory\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("vm_iomap_memory mmap failed: %s\n",
				   strerror(errno));

	sigemptyset(&action.sa_mask);
	if (sigaction(SIGSEGV, &action, NULL) ||
	    sigaction(SIGBUS, &action, NULL))
		ksft_exit_fail_msg("sigaction failed: %s\n", strerror(errno));
	ksft_test_result(read_byte(mapping, &value) && value == FIRST_MARKER,
			 "the requested 4K slice is readable\n");

	for (guard = 1; guard < 4; guard++) {
		if (read_byte(mapping + guard * PROCESS_PAGE_SIZE, &value)) {
			ksft_print_msg("guard %u is readable with value %#x\n",
				       guard, value);
			guards_fault = false;
		}
	}
	ksft_test_result(guards_fault,
			 "vm_iomap_memory does not populate adjacent guard slices\n");
	munmap(reservation, NATIVE_PAGE_SIZE);

	reservation = mmap(NULL, PRIVATE_RESERVATION_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		ksft_exit_fail_msg("private guard reservation failed: %s\n",
				   strerror(errno));
	target_address = ((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
		~(NATIVE_PAGE_SIZE - 1);
	mapping = mmap((void *)target_address, PROCESS_PAGE_SIZE,
		       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, 0);
	ksft_test_result(mapping == (void *)target_address,
			 "map one private PFN-backed process page\n");
	if (mapping != (void *)target_address)
		ksft_exit_fail_msg("private mmap failed: %s\n",
				   strerror(errno));
	mapping_ok = mapping_has_values(mapping, PROCESS_PAGE_SIZE);
	ksft_test_result(mapping_ok,
			 "private PFN mapping initially contains device data\n");
	if (!mapping_ok)
		ksft_exit_fail_msg("private mapping data mismatch\n");
	mapping[COW_OFFSET] = COW_MARKER;
	ksft_test_result(mapping_has_values(mapping, COW_OFFSET),
			 "COW preserves the complete process page\n");
	munmap(reservation, PRIVATE_RESERVATION_SIZE);

	reservation = mmap(NULL, NATIVE_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		ksft_exit_fail_msg("offset guard reservation failed: %s\n",
				   strerror(errno));
	mapping = mmap(reservation, PROCESS_PAGE_SIZE, PROT_READ,
		       MAP_SHARED | MAP_FIXED, fd, PROCESS_PAGE_SIZE);
	ksft_test_result(mapping == reservation,
			 "map one 4K slice at physical buffer offset 4K\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("offset mmap failed: %s\n", strerror(errno));
	value = 0;
	ksft_test_result(read_byte(mapping, &value) && value == FIRST_MARKER + 1,
			 "the offset mapping starts at the second 4K slice\n");

	munmap(reservation, NATIVE_PAGE_SIZE);
	past_end = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ, MAP_SHARED, fd,
			NATIVE_PAGE_SIZE);
	ksft_test_result(past_end == MAP_FAILED,
			 "reject a 4K mapping starting at the buffer end\n");
	if (past_end != MAP_FAILED)
		munmap(past_end, PROCESS_PAGE_SIZE);

	unaligned_fd = ppps_open_fixture_or_skip("/dev/vm_iomap_unaligned_ppps", O_RDWR);
	ksft_test_result(unaligned_fd >= 0,
			 "open the unaligned vm_iomap_memory test device\n");
	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ, MAP_SHARED,
		       unaligned_fd, 0);
	value = 0;
	ksft_test_result(mapping != MAP_FAILED &&
			 read_byte(mapping, &value) && value == FIRST_MARKER + 1,
			 "an unaligned resource maps its first physical 4K slice\n");
	if (mapping != MAP_FAILED)
		munmap(mapping, PROCESS_PAGE_SIZE);
	past_end = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ, MAP_SHARED,
			unaligned_fd, PROCESS_PAGE_SIZE);
	ksft_test_result(past_end == MAP_FAILED,
			 "reject an offset at the end of an unaligned resource\n");
	if (past_end != MAP_FAILED)
		munmap(past_end, PROCESS_PAGE_SIZE);
	close(unaligned_fd);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
