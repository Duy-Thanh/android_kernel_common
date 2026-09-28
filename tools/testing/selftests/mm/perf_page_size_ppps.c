// SPDX-License-Identifier: GPL-2.0
/*
 * A minor-fault perf sample taken by a 4K compat process reports the 4K
 * process page size in PERF_SAMPLE_DATA_PAGE_SIZE and
 * PERF_SAMPLE_CODE_PAGE_SIZE.
 */
#define _GNU_SOURCE

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

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

static bool find_page_sizes(struct perf_event_mmap_page *metadata,
			    uint64_t expected_addr, uint64_t *data_page_size,
			    uint64_t *code_page_size)
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
		    header.size >= sizeof(header) + 3 * sizeof(uint64_t)) {
			uint64_t sample[3];

			copy_ring(sample, data, data_size,
				  position + sizeof(header), sizeof(sample));
			if (sample[0] == expected_addr) {
				*data_page_size = sample[1];
				*code_page_size = sample[2];
				found = true;
			}
		}
		position += header.size;
	}
	__atomic_store_n(&metadata->data_tail, position, __ATOMIC_RELEASE);
	return found;
}

static int sample_fault_page_sizes(unsigned char *address,
				   uint64_t *data_page_size,
				   uint64_t *code_page_size)
{
	struct perf_event_attr attr = {
		.type = PERF_TYPE_SOFTWARE,
		.size = sizeof(attr),
		.config = PERF_COUNT_SW_PAGE_FAULTS_MIN,
		.sample_period = 1,
		.sample_type = PERF_SAMPLE_ADDR | PERF_SAMPLE_DATA_PAGE_SIZE |
			       PERF_SAMPLE_CODE_PAGE_SIZE,
		.disabled = 1,
		.exclude_kernel = 1,
		.exclude_hv = 1,
		.wakeup_events = 1,
	};
	struct perf_event_mmap_page *metadata;
	int fd;
	int ret = -1;

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
	if (find_page_sizes(metadata, (uintptr_t)address, data_page_size,
			    code_page_size))
		ret = 0;

out_unmap:
	munmap(metadata, 2 * PROCESS_PAGE_SIZE);
out_close:
	close(fd);
	return ret;
}

static int run_test(void)
{
	unsigned char *address;
	uint64_t data_page_size = 0;
	uint64_t code_page_size = 0;
	int sampled;

	ksft_print_header();
	ksft_set_plan(4);

	address = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ksft_test_result(address != MAP_FAILED,
			 "create an unfaulted anonymous mapping\n");
	if (address == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));

	sampled = sample_fault_page_sizes(address, &data_page_size,
					  &code_page_size);
	ksft_test_result(sampled == 0,
			 "capture page sizes from a minor-fault sample\n");
	ksft_print_msg("data_page_size=%llu code_page_size=%llu\n",
		       (unsigned long long)data_page_size,
		       (unsigned long long)code_page_size);
	ksft_test_result(sampled == 0 && data_page_size == PROCESS_PAGE_SIZE,
			 "PERF_SAMPLE_DATA_PAGE_SIZE uses process page size\n");
	ksft_test_result(sampled == 0 && code_page_size == PROCESS_PAGE_SIZE,
			 "PERF_SAMPLE_CODE_PAGE_SIZE uses process page size\n");

	munmap(address, PROCESS_PAGE_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
