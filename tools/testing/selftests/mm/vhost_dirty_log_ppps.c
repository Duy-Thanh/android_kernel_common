// SPDX-License-Identifier: GPL-2.0
/*
 * vhost dirty logging for a 4K compat process: a consumed descriptor sets the
 * dirty bit in a 4K log page mapped at a 4K offset inside a native page, and
 * the adjacent file data of the log stays untouched.
 */
#define _GNU_SOURCE
#include <linux/vhost.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"
#include "../../../../drivers/vhost/test.h"

#ifndef VHOST_F_LOG_ALL
#define VHOST_F_LOG_ALL 26
#endif

#define VRING_NUM 8
#define DATA_SIZE (4 * PROCESS_PAGE_SIZE)
#define BUFFER_OFFSET (2 * PROCESS_PAGE_SIZE)

static int failures;
static int tests;

static void report(int ok, const char *name)
{
	tests++;
	printf("%s %d - %s\n", ok ? "ok" : "not ok", tests, name);
	if (!ok)
		failures++;
}

static int xioctl(int fd, unsigned long request, void *arg,
		  const char *name)
{
	if (ioctl(fd, request, arg) == 0)
		return 0;

	fprintf(stderr, "%s: %s\n", name, strerror(errno));
	return -1;
}

static void *map_log_page(int *fd_out, void **reservation_out)
{
	char path[] = "/tmp/vhost-log-XXXXXX";
	uintptr_t aligned;
	void *reservation;
	void *log;
	int fd;

	fd = mkstemp(path);
	if (fd < 0)
		return MAP_FAILED;
	unlink(path);
	if (ftruncate(fd, NATIVE_PAGE_SIZE))
		goto err;

	reservation = mmap(NULL, 3 * NATIVE_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		goto err;
	aligned = ((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
		  ~(NATIVE_PAGE_SIZE - 1);
	log = mmap((void *)(aligned + PROCESS_PAGE_SIZE), PROCESS_PAGE_SIZE,
		   PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
	if (log == MAP_FAILED) {
		munmap(reservation, 3 * NATIVE_PAGE_SIZE);
		goto err;
	}

	*fd_out = fd;
	*reservation_out = reservation;
	return log;

err:
	close(fd);
	return MAP_FAILED;
}

static int run_test(void)
{
	struct vhost_memory *mem = NULL;
	struct vhost_vring_state state = { .index = 0 };
	struct vhost_vring_file file = { .index = 0 };
	struct vhost_vring_addr addr = { .index = 0 };
	struct pollfd pfd;
	struct vring vring;
	uint64_t features = 1ULL << VHOST_F_LOG_ALL;
	uint64_t log_base;
	uint64_t event;
	void *reservation = MAP_FAILED;
	void *data = MAP_FAILED;
	unsigned char *buffer;
	unsigned char *log = MAP_FAILED;
	unsigned char neighbor = 0;
	int log_fd = -1;
	int kick = -1;
	int call = -1;
	int control = -1;
	int run = 1;
	int ret = KSFT_FAIL;

	printf("TAP version 13\n");

	control = open("/dev/vhost-test", O_RDWR | O_CLOEXEC);
	if (control < 0) {
		printf("1..0 # SKIP /dev/vhost-test unavailable\n");
		return KSFT_SKIP;
	}

	log = map_log_page(&log_fd, &reservation);
	data = mmap(NULL, DATA_SIZE, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	kick = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	call = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (log == MAP_FAILED || data == MAP_FAILED || kick < 0 || call < 0)
		goto out;

	mem = calloc(1, sizeof(*mem) + sizeof(mem->regions[0]));
	if (!mem)
		goto out;
	mem->nregions = 1;
	mem->regions[0].guest_phys_addr = (uintptr_t)data;
	mem->regions[0].userspace_addr = (uintptr_t)data;
	mem->regions[0].memory_size = DATA_SIZE;

	memset(data, 0, DATA_SIZE);
	vring_init(&vring, VRING_NUM, data, PROCESS_PAGE_SIZE);
	buffer = data + BUFFER_OFFSET;
	memset(buffer, 0xa5, 64);
	vring.desc[0].addr = (uintptr_t)buffer;
	vring.desc[0].len = 64;
	vring.desc[0].flags = 0;
	vring.avail->ring[0] = 0;
	vring.avail->idx = 1;

	if (xioctl(control, VHOST_SET_OWNER, NULL, "VHOST_SET_OWNER"))
		goto out;
	if (xioctl(control, VHOST_SET_MEM_TABLE, mem,
		   "VHOST_SET_MEM_TABLE"))
		goto out;
	log_base = (uintptr_t)log;
	if (xioctl(control, VHOST_SET_LOG_BASE, &log_base,
		   "VHOST_SET_LOG_BASE"))
		goto out;
	if (xioctl(control, VHOST_SET_FEATURES, &features,
		   "VHOST_SET_FEATURES"))
		goto out;

	state.num = VRING_NUM;
	if (xioctl(control, VHOST_SET_VRING_NUM, &state,
		   "VHOST_SET_VRING_NUM"))
		goto out;
	state.num = 0;
	if (xioctl(control, VHOST_SET_VRING_BASE, &state,
		   "VHOST_SET_VRING_BASE"))
		goto out;

	addr.flags = 1U << VHOST_VRING_F_LOG;
	addr.desc_user_addr = (uintptr_t)vring.desc;
	addr.avail_user_addr = (uintptr_t)vring.avail;
	addr.used_user_addr = (uintptr_t)vring.used;
	addr.log_guest_addr = PROCESS_PAGE_SIZE;
	if (xioctl(control, VHOST_SET_VRING_ADDR, &addr,
		   "VHOST_SET_VRING_ADDR"))
		goto out;

	file.fd = kick;
	if (xioctl(control, VHOST_SET_VRING_KICK, &file,
		   "VHOST_SET_VRING_KICK"))
		goto out;
	file.fd = call;
	if (xioctl(control, VHOST_SET_VRING_CALL, &file,
		   "VHOST_SET_VRING_CALL"))
		goto out;
	if (xioctl(control, VHOST_TEST_RUN, &run, "VHOST_TEST_RUN"))
		goto out;

	event = 1;
	if (write(kick, &event, sizeof(event)) != sizeof(event))
		goto out;
	pfd.fd = call;
	pfd.events = POLLIN;
	if (poll(&pfd, 1, 5000) != 1)
		goto out;
	if (read(call, &event, sizeof(event)) != sizeof(event))
		goto out;

	report(vring.used->idx == 1, "vhost consumed descriptor");
	report((*log & (1U << 1)) != 0,
	       "dirty bit is visible in the 4K process mapping");
	report(pread(log_fd, &neighbor, 1, PROCESS_PAGE_SIZE) == 1 &&
	       neighbor == 0, "adjacent file data remains unchanged");
	printf("1..%d\n", tests);
	ret = failures ? KSFT_FAIL : KSFT_PASS;

out:
	if (ret == KSFT_FAIL && !tests)
		fprintf(stderr, "test setup or execution failed\n");
	if (control >= 0)
		close(control);
	if (call >= 0)
		close(call);
	if (kick >= 0)
		close(kick);
	free(mem);
	if (data != MAP_FAILED)
		munmap(data, DATA_SIZE);
	if (reservation != MAP_FAILED)
		munmap(reservation, 3 * NATIVE_PAGE_SIZE);
	if (log_fd >= 0)
		close(log_fd);
	return ret;
}

PPPS_COMPAT_MAIN(run_test)
