// SPDX-License-Identifier: GPL-2.0
/*
 * PERF_SAMPLE_PHYS_ADDR from read watchpoints on two 4K aliases of the same
 * resident memfd byte, mapped at different offsets within a native page,
 * reports the same physical address in a 4K compat process. A minor-fault
 * event fires before the PTE exists and cannot supply that address.
 */
#define _GNU_SOURCE

#include <linux/memfd.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

#define RESERVE_SIZE		(8 * NATIVE_PAGE_SIZE)

static int perf_event_open(struct perf_event_attr *attr)
{
	return syscall(__NR_perf_event_open, attr, 0, -1, -1, 0);
}

static void copy_ring(void *destination, const unsigned char *data,
		      size_t data_size, uint64_t position, size_t length)
{
	size_t offset = position % data_size;
	size_t first = data_size - offset;

	if (first > length)
		first = length;
	memcpy(destination, data + offset, first);
	if (first < length)
		memcpy((unsigned char *)destination + first, data,
		       length - first);
}

static bool find_phys_addr(struct perf_event_mmap_page *metadata,
			   uint64_t expected_addr, uint64_t *phys_addr)
{
	size_t data_offset = metadata->data_offset ?: PROCESS_PAGE_SIZE;
	size_t data_size = metadata->data_size ?: PROCESS_PAGE_SIZE;
	const unsigned char *data = (const unsigned char *)metadata + data_offset;
	uint64_t head = __atomic_load_n(&metadata->data_head, __ATOMIC_ACQUIRE);
	uint64_t position = metadata->data_tail;
	bool found = false;

	while (position < head) {
		struct perf_event_header header;

		copy_ring(&header, data, data_size, position, sizeof(header));
		if (header.size < sizeof(header) || header.size > data_size)
			break;
		if (header.type == PERF_RECORD_SAMPLE &&
		    header.size >= sizeof(header) + 2 * sizeof(uint64_t)) {
			uint64_t sample[2];

			copy_ring(sample, data, data_size,
				  position + sizeof(header), sizeof(sample));
			if (sample[0] == expected_addr) {
				*phys_addr = sample[1];
				found = true;
			}
		}
		position += header.size;
	}
	__atomic_store_n(&metadata->data_tail, position, __ATOMIC_RELEASE);
	return found;
}

static int sample_fault_phys_addr(unsigned char *address,
				  uint64_t *phys_addr)
{
	struct perf_event_attr attr = {
		.type = PERF_TYPE_BREAKPOINT,
		.size = sizeof(attr),
		.bp_type = HW_BREAKPOINT_R,
		.bp_addr = (uintptr_t)address,
		.bp_len = HW_BREAKPOINT_LEN_1,
		.sample_period = 1,
		.sample_type = PERF_SAMPLE_ADDR | PERF_SAMPLE_PHYS_ADDR,
		.disabled = 1,
		.exclude_kernel = 1,
		.exclude_hv = 1,
		.wakeup_events = 1,
	};
	struct perf_event_mmap_page *metadata;
	int fd;
	int ret = -1;

	/* The watched address must already have a PTE when perf samples it. */
	(void)__atomic_load_n(address, __ATOMIC_RELAXED);
	fd = perf_event_open(&attr);
	if (fd < 0)
		return -1;
	metadata = mmap(NULL, 2 * PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
	if (metadata == MAP_FAILED)
		goto out_close;
	if (ioctl(fd, PERF_EVENT_IOC_RESET, 0) ||
	    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0))
		goto out_unmap;

	(void)__atomic_load_n(address, __ATOMIC_RELAXED);
	__asm__ __volatile__("" : : : "memory");
	if (ioctl(fd, PERF_EVENT_IOC_DISABLE, 0))
		goto out_unmap;
	if (find_phys_addr(metadata, (uintptr_t)address, phys_addr))
		ret = 0;

out_unmap:
	munmap(metadata, 2 * PROCESS_PAGE_SIZE);
out_close:
	close(fd);
	return ret;
}

static int run_test(void)
{
	unsigned char *alias0 = MAP_FAILED;
	unsigned char *alias1 = MAP_FAILED;
	void *reservation;
	uintptr_t base;
	uint64_t phys0 = 0;
	uint64_t phys1 = 0;
	bool mappings_ok;
	int memfd;
	int sample0;
	int sample1;

	memfd = memfd_create("perf-phys-addr-ppps", MFD_CLOEXEC);
	if (memfd < 0 || ftruncate(memfd, PROCESS_PAGE_SIZE))
		ksft_exit_fail_msg("memfd setup failed: %s\n", strerror(errno));
	reservation = mmap(NULL, RESERVE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		ksft_exit_fail_msg("address reservation failed: %s\n",
				   strerror(errno));
	base = ((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
		~(NATIVE_PAGE_SIZE - 1);
	munmap(reservation, RESERVE_SIZE);

	alias0 = mmap((void *)base, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_FIXED_NOREPLACE, memfd, 0);
	alias1 = mmap((void *)(base + NATIVE_PAGE_SIZE + PROCESS_PAGE_SIZE),
		      PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_FIXED_NOREPLACE, memfd, 0);
	mappings_ok = alias0 != MAP_FAILED && alias1 != MAP_FAILED;
	if (!mappings_ok)
		ksft_exit_fail_msg("alias mmap failed: %s\n", strerror(errno));

	sample0 = sample_fault_phys_addr(alias0, &phys0);
	sample1 = sample_fault_phys_addr(alias1, &phys1);
	if (!sample0 && !sample1 && !phys0 && !phys1)
		ksft_exit_skip("perf watchpoints supplied no physical addresses\n");
	ksft_print_header();
	ksft_set_plan(5);
	ksft_test_result(mappings_ok,
			 "map the same file byte at native offsets 0 and 4K\n");
	ksft_test_result(sample0 == 0 && phys0,
			 "sample a resident physical address at native offset 0\n");
	ksft_test_result(sample1 == 0 && phys1,
			 "sample a resident physical address at native offset 4K\n");
	__atomic_store_n(alias0, 0x5a, __ATOMIC_RELAXED);
	ksft_test_result(__atomic_load_n(alias1, __ATOMIC_RELAXED) == 0x5a,
			 "aliases resolve to the same file byte\n");
	ksft_print_msg("phys0=%#llx phys1=%#llx delta=%lld\n",
		       (unsigned long long)phys0, (unsigned long long)phys1,
		       (long long)(phys1 - phys0));
	ksft_test_result(sample0 == 0 && sample1 == 0 && phys0 &&
			 phys0 == phys1,
			 "PERF_SAMPLE_PHYS_ADDR identifies the same physical byte\n");

	munmap(alias1, PROCESS_PAGE_SIZE);
	munmap(alias0, PROCESS_PAGE_SIZE);
	close(memfd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
