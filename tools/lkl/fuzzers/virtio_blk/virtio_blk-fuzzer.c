/* SPDX-License-Identifier: GPL-2.0 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/sysmacros.h>
#include <sys/mman.h>

#include <lkl.h>
#include <lkl_host.h>

#include "../../lib/virtio.h"

#define NUM_VIRTIO_FUZZ     16
#define QUEUES_VIRTIO_FUZZ  1

#define LOG(fmt, ...)                                                  \
	if (g_log_enabled) {                                               \
		printf(fmt, ##__VA_ARGS__);                                    \
	}

static bool g_log_enabled = true;

static int g_fuzz_iterations = 0;

static int lkl_init()
{
	if (!g_log_enabled) {
		lkl_host_ops.print = NULL;
	}

	int ret = lkl_start_kernel(&lkl_host_ops, "mem=50M loglevel=8");
	if (ret) {
		LOG("lkl_start_kernel failed\n");
		return -1;
	}

	lkl_mount_fs("sysfs");
	lkl_mount_fs("proc");
	lkl_mount_fs("dev");

	return 0;
}

void __llvm_profile_initialize_file(void);
int __llvm_profile_write_file(void);

void flush_coverage()
{
	LOG("Flushing coverage data...\n");
	__llvm_profile_write_file();
	LOG("Done...\n");
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	for (int i = 0; i < *argc; i++) {
		if (strcmp((*argv)[i], "-quiet=1") == 0) {
			g_log_enabled = false;
			break;
		}
	}

	lkl_init();

	__llvm_profile_initialize_file();
	atexit(flush_coverage);
	return 0;
}

static int blk_check_features(struct virtio_dev *dev)
{
	if (dev->driver_features == dev->device_features)
		return 0;

	return -LKL_EINVAL;
}

struct virtio_blk_req_trailer {
	uint8_t status;
};

static int blk_enqueue(struct virtio_dev *dev, int q, struct virtio_req *req)
{
	struct virtio_blk_req_trailer *t;

	// This is invoked to perform block read/write operations. For fuzzing
	// purposes do nothing and return success. This will force the virtio_blk
	// driver that the operation has completed successfully.
	t = req->buf[req->buf_count - 1].iov_base;
	t->status = LKL_DEV_BLK_STATUS_OK;

	virtio_req_complete(req, 0);
	return 0;
}

static struct virtio_dev_ops blk_ops = {
	.check_features = blk_check_features,
	.enqueue = blk_enqueue,
};

struct fuzzer_data {
	uint32_t dev_features;
	struct lkl_virtio_blk_config config;
};

// Current implementation fuzzes a config block of virtio_blk device only.
// Fuzzing virtio ring code is implemented in virtio_ring-fuzzer.
int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	struct fuzzer_data fuzz_data;
	struct virtio_dev dev;

	LOG("Begin fuzz iteration.\n");

	g_fuzz_iterations++;
	if (g_fuzz_iterations > 1000) {
		flush_coverage();
		g_fuzz_iterations = 0;
	}

	memset(&dev, 0x00, sizeof(dev));
	memset(&fuzz_data, 0x00, sizeof(fuzz_data));

	memcpy(&fuzz_data, Data, Size < sizeof(fuzz_data) ? Size : sizeof(fuzz_data));

	dev.device_id = LKL_VIRTIO_ID_BLOCK;
	dev.vendor_id = 0;
	dev.device_features = fuzz_data.dev_features;
	dev.config_gen = 0;
	dev.config_data = &fuzz_data.config;
	dev.config_len = sizeof(fuzz_data.config);
	dev.ops = &blk_ops;

	// Add virtio_blk over virtio_mmio which will trigger initialization of
	// vda block device.
	assert(virtio_dev_setup(&dev, QUEUES_VIRTIO_FUZZ, NUM_VIRTIO_FUZZ) == 0);
	assert(virtio_dev_cleanup(&dev) == 0);

	LOG("Done.\n");

	return 0;
}
