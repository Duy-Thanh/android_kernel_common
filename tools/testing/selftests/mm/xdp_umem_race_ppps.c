// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/if_xdp.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include "kselftest_ppps.h"

struct xdp_pin_probe {
	uint64_t address[2];
	uint32_t pinned[2];
	uint32_t order[2];
	int64_t refs[2];
	uint64_t pfn[2];
};

#define XDP_PIN_PROBE_IOCTL _IOWR(0xa4, 1, struct xdp_pin_probe)
#define MAX_ATTEMPTS 4096

struct race_context {
	void *target;
	int file_a;
	int file_b;
	atomic_bool stop;
	atomic_ulong map_failures;
};

static int map_target(void *target, int fd, off_t offset)
{
	void *result;

	result = mmap(target, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_FIXED, fd, offset);
	return result == target ? 0 : -1;
}

static void *toggle_mapping(void *argument)
{
	struct race_context *context = argument;
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(1, &set);
	sched_setaffinity(0, sizeof(set), &set);
	while (!atomic_load_explicit(&context->stop, memory_order_relaxed)) {
		if (map_target(context->target, context->file_a, 0))
			atomic_fetch_add(&context->map_failures, 1);
		sched_yield();
		if (map_target(context->target, context->file_b,
			       PROCESS_PAGE_SIZE))
			atomic_fetch_add(&context->map_failures, 1);
		sched_yield();
	}
	return NULL;
}

static int query_pages(int probe_fd, void *stable_a, void *stable_b,
		       struct xdp_pin_probe *probe)
{
	memset(probe, 0, sizeof(*probe));
	probe->address[0] = (uintptr_t)stable_a;
	probe->address[1] = (uintptr_t)stable_b;
	return ioctl(probe_fd, XDP_PIN_PROBE_IOCTL, probe);
}

static int register_umem(void *target)
{
	struct xdp_umem_reg registration = {
		.addr = (uintptr_t)target,
		.len = PROCESS_PAGE_SIZE,
		.chunk_size = 2048,
	};
	int fd;

	fd = socket(AF_XDP, SOCK_RAW | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -errno;
	if (setsockopt(fd, SOL_XDP, XDP_UMEM_REG, &registration,
		       sizeof(registration))) {
		int error = errno;

		close(fd);
		return -error;
	}
	return fd;
}

static int wait_unpinned(int probe_fd, void *stable_a, void *stable_b)
{
	struct xdp_pin_probe probe;
	unsigned int attempt;

	for (attempt = 0; attempt < 10000; attempt++) {
		if (query_pages(probe_fd, stable_a, stable_b, &probe))
			return -1;
		if (!probe.pinned[0] && !probe.pinned[1])
			return 0;
		usleep(100);
	}
	errno = ETIMEDOUT;
	return -1;
}

static int run_test(void)
{
	struct race_context context = {};
	struct xdp_pin_probe probe;
	unsigned long accepted = 0;
	unsigned long rejected = 0;
	unsigned long other = 0;
	pthread_t toggler;
	void *stable_a;
	void *stable_b;
	void *reservation;
	cpu_set_t set;
	int probe_fd;
	int cleanup_error = 0;
	int hit = 0;
	int i;

	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(5);
	context.file_a = memfd_create("xdp-race-a", MFD_CLOEXEC);
	context.file_b = memfd_create("xdp-race-b", MFD_CLOEXEC);
	if (context.file_a < 0 || context.file_b < 0 ||
	    ftruncate(context.file_a, NATIVE_PAGE_SIZE) ||
	    ftruncate(context.file_b, NATIVE_PAGE_SIZE))
		ksft_exit_fail_msg("memfd setup failed: %s\n", strerror(errno));
	stable_a = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
			MAP_SHARED, context.file_a, 0);
	stable_b = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
			MAP_SHARED, context.file_b, 0);
	reservation = mmap(NULL, NATIVE_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (stable_a == MAP_FAILED || stable_b == MAP_FAILED ||
	    reservation == MAP_FAILED)
		ksft_exit_fail_msg("mmap setup failed: %s\n", strerror(errno));
	if ((uintptr_t)reservation & (NATIVE_PAGE_SIZE - 1))
		ksft_exit_fail_msg("target is not native aligned\n");
	context.target = reservation;
	*(unsigned char *)stable_a = 0xa1;
	*(unsigned char *)stable_b = 0xb2;
	probe_fd = open("/dev/xdp_pin_probe_ppps", O_RDWR | O_CLOEXEC);
	if (probe_fd < 0)
		ksft_exit_fail_msg("open probe failed: %s\n", strerror(errno));
	if (query_pages(probe_fd, stable_a, stable_b, &probe))
		ksft_exit_fail_msg("initial probe failed: %s\n", strerror(errno));
	printf("BASE pinned=%u,%u order=%u,%u refs=%lld,%lld pfn=%llu,%llu\n",
	       probe.pinned[0], probe.pinned[1], probe.order[0], probe.order[1],
	       (long long)probe.refs[0], (long long)probe.refs[1],
	       (unsigned long long)probe.pfn[0],
	       (unsigned long long)probe.pfn[1]);
	ksft_test_result(!probe.pinned[0] && !probe.pinned[1] &&
			 probe.pfn[0] != probe.pfn[1],
			 "probe pages are independent and initially unpinned\n");
	if (map_target(context.target, context.file_a, 0))
		ksft_exit_fail_msg("initial target map failed\n");

	CPU_ZERO(&set);
	CPU_SET(0, &set);
	sched_setaffinity(0, sizeof(set), &set);
	if (pthread_create(&toggler, NULL, toggle_mapping, &context))
		ksft_exit_fail_msg("pthread_create failed\n");

	for (i = 0; i < MAX_ATTEMPTS; i++) {
		int fd = register_umem(context.target);

		if (fd >= 0) {
			accepted++;
			if (query_pages(probe_fd, stable_a, stable_b, &probe))
				ksft_exit_fail_msg("probe failed: %s\n",
						   strerror(errno));
			if (probe.pinned[1]) {
				printf("A4_RACE_HIT attempt=%d pinned=%u,%u\n",
				       i, probe.pinned[0], probe.pinned[1]);
				printf("A4_RACE_STATE refs=%lld,%lld pfn=%llu,%llu\n",
				       (long long)probe.refs[0],
				       (long long)probe.refs[1],
				       (unsigned long long)probe.pfn[0],
				       (unsigned long long)probe.pfn[1]);
				hit = 1;
				close(fd);
				cleanup_error = wait_unpinned(probe_fd, stable_a,
							      stable_b);
				break;
			}
			close(fd);
			if (wait_unpinned(probe_fd, stable_a, stable_b))
				ksft_exit_fail_msg("AF_XDP pin cleanup timed out: %s\n",
						   strerror(errno));
		} else if (fd == -EOPNOTSUPP) {
			rejected++;
		} else if (fd == -EAFNOSUPPORT || fd == -EPROTONOSUPPORT) {
			atomic_store(&context.stop, true);
			pthread_join(toggler, NULL);
			ksft_exit_skip("AF_XDP unavailable: %s\n", strerror(-fd));
		} else {
			other++;
		}
		if (!(i % 10000))
			printf("PROGRESS attempts=%d accepted=%lu rejected=%lu other=%lu\n",
			       i, accepted, rejected, other);
	}
	atomic_store(&context.stop, true);
	pthread_join(toggler, NULL);
	printf("A4_RACE_DONE hit=%d accepted=%lu rejected=%lu other=%lu map_failures=%lu\n",
	       hit, accepted, rejected, other,
	       atomic_load(&context.map_failures));
	ksft_test_result(accepted > 0,
			 "AF_XDP accepted at least one slice-zero registration\n");
	ksft_test_result(!hit,
			 "AF_XDP never pins a nonzero-slice replacement\n");
	ksft_test_result(!other && !atomic_load(&context.map_failures),
			 "race operations return only expected results\n");
	ksft_test_result(!cleanup_error,
			 "all AF_XDP long-term pins are released\n");
	close(probe_fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
