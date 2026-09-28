// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process maps the consumer, producer and data pages of BPF
 * kernel and user ring buffers in 4K units, with the expected protections
 * and bounds.
 */
#define _GNU_SOURCE

#include <linux/bpf.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

#define SMALL_RING_SIZE	PROCESS_PAGE_SIZE
#define RING_SIZE	(4 * PROCESS_PAGE_SIZE)
#define RING_MMAP_SIZE	(2 * PROCESS_PAGE_SIZE + 2 * RING_SIZE)

static int sys_bpf(enum bpf_cmd command, union bpf_attr *attr)
{
	return syscall(__NR_bpf, command, attr, sizeof(*attr));
}

static int create_ringbuf(enum bpf_map_type type, size_t size)
{
	union bpf_attr attr = {
		.map_type = type,
		.max_entries = size,
	};

	return sys_bpf(BPF_MAP_CREATE, &attr);
}

static bool mapping_succeeds(int fd, size_t length, int prot, off_t offset)
{
	void *mapping;

	mapping = mmap(NULL, length, prot, MAP_SHARED, fd, offset);
	if (mapping == MAP_FAILED) {
		ksft_print_msg("mmap length=%zu prot=%#x offset=%lld failed: %s\n",
			       length, prot, (long long)offset, strerror(errno));
		return false;
	}
	munmap(mapping, length);
	return true;
}

static bool mapping_fails(int fd, size_t length, int prot, off_t offset,
			  int expected_errno)
{
	void *mapping;

	errno = 0;
	mapping = mmap(NULL, length, prot, MAP_SHARED, fd, offset);
	if (mapping != MAP_FAILED) {
		munmap(mapping, length);
		return false;
	}
	if (errno != expected_errno)
		ksft_print_msg("mmap length=%zu prot=%#x offset=%lld errno=%d, expected=%d\n",
			       length, prot, (long long)offset, errno,
			       expected_errno);
	return errno == expected_errno;
}

static int run_test(void)
{
	struct rlimit memlock = {
		.rlim_cur = RLIM_INFINITY,
		.rlim_max = RLIM_INFINITY,
	};
	int ringbuf_fd;
	int small_ringbuf_fd;
	int user_ringbuf_fd;
	bool mapping_ok;

	ksft_print_header();
	ksft_set_plan(15);
	setrlimit(RLIMIT_MEMLOCK, &memlock);

	small_ringbuf_fd = create_ringbuf(BPF_MAP_TYPE_RINGBUF,
					  SMALL_RING_SIZE);
	if (small_ringbuf_fd < 0)
		ksft_exit_fail_msg("4K BPF ringbuf creation failed: %s\n",
				   strerror(errno));
	ksft_test_result(true, "create a 4K kernel ring buffer\n");
	mapping_ok = mapping_succeeds(small_ringbuf_fd, PROCESS_PAGE_SIZE,
				      PROT_READ | PROT_WRITE, 0);
	ksft_test_result(mapping_ok,
			 "map the 4K ring's consumer position\n");
	mapping_ok = mapping_succeeds(small_ringbuf_fd, PROCESS_PAGE_SIZE,
				      PROT_READ, PROCESS_PAGE_SIZE);
	ksft_test_result(mapping_ok,
			 "map the 4K ring's producer position\n");
	mapping_ok = mapping_succeeds(small_ringbuf_fd, PROCESS_PAGE_SIZE,
				      PROT_READ, 2 * PROCESS_PAGE_SIZE);
	ksft_test_result(mapping_ok, "map the 4K ring's data page\n");
	close(small_ringbuf_fd);

	ringbuf_fd = create_ringbuf(BPF_MAP_TYPE_RINGBUF, RING_SIZE);
	if (ringbuf_fd < 0) {
		if (errno == EPERM || errno == EACCES)
			ksft_exit_skip("BPF map creation is unavailable: %s\n",
				       strerror(errno));
		ksft_exit_fail_msg("BPF ringbuf creation failed: %s\n",
				   strerror(errno));
	}
	ksft_test_result(true, "create a kernel ring buffer\n");
	mapping_ok = mapping_succeeds(ringbuf_fd, PROCESS_PAGE_SIZE,
				      PROT_READ | PROT_WRITE, 0);
	ksft_test_result(mapping_ok,
			 "map the 4K consumer position read-write\n");
	mapping_ok = mapping_succeeds(ringbuf_fd, PROCESS_PAGE_SIZE, PROT_READ,
				      PROCESS_PAGE_SIZE);
	ksft_test_result(mapping_ok,
			 "map the 4K producer position read-only\n");
	mapping_ok = mapping_succeeds(ringbuf_fd, PROCESS_PAGE_SIZE, PROT_READ,
				      2 * PROCESS_PAGE_SIZE);
	ksft_test_result(mapping_ok,
			 "map the first 4K data slice read-only\n");
	mapping_ok = mapping_succeeds(ringbuf_fd, RING_MMAP_SIZE, PROT_READ, 0);
	ksft_test_result(mapping_ok,
			 "map the complete logical ring buffer\n");
	mapping_ok = mapping_fails(ringbuf_fd, 2 * PROCESS_PAGE_SIZE, PROT_READ,
				   RING_MMAP_SIZE - PROCESS_PAGE_SIZE, EINVAL);
	ksft_test_result(mapping_ok,
			 "reject a mapping past the logical ring buffer\n");
	close(ringbuf_fd);

	user_ringbuf_fd = create_ringbuf(BPF_MAP_TYPE_USER_RINGBUF, RING_SIZE);
	if (user_ringbuf_fd < 0)
		ksft_exit_fail_msg("BPF user ringbuf creation failed: %s\n",
				   strerror(errno));
	ksft_test_result(true, "create a user ring buffer\n");
	mapping_ok = mapping_succeeds(user_ringbuf_fd, PROCESS_PAGE_SIZE,
				      PROT_READ, 0);
	ksft_test_result(mapping_ok,
			 "map the user consumer position read-only\n");
	mapping_ok = mapping_succeeds(user_ringbuf_fd, PROCESS_PAGE_SIZE,
				      PROT_READ | PROT_WRITE, PROCESS_PAGE_SIZE);
	ksft_test_result(mapping_ok,
			 "map the user producer position read-write\n");
	mapping_ok = mapping_succeeds(user_ringbuf_fd, PROCESS_PAGE_SIZE,
				      PROT_READ | PROT_WRITE,
				      2 * PROCESS_PAGE_SIZE);
	ksft_test_result(mapping_ok,
			 "map the first user data slice read-write\n");
	mapping_ok = mapping_fails(user_ringbuf_fd, PROCESS_PAGE_SIZE,
				   PROT_READ | PROT_WRITE, 0, EPERM);
	ksft_test_result(mapping_ok,
			 "reject a writable user consumer position\n");
	close(user_ringbuf_fd);

	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
