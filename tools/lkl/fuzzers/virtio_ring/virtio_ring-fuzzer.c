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

#include "../../lib/iomem.h"
#include "../../lib/virtio.h"

#include "log.h"

// Reduce size of the virtio queue size to 8 elements in order to simplify
// the job for the fuzzer: see definition of struct lkl_virtio_mutated_ring
// below which demonstrates relationship between virio queue size and the
// fuzzing search space.
#define VIRTIO_QUEUE_SIZE 8

// Some device types (for example, VIRTIO_INPUT) require at minimium 2 queues.
#define NUM_OF_VIRTIO_QUEUES 2

#define VIRTIO_DEV_MAGIC 0x74726976
#define VIRTIO_DEV_VERSION 2

#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_VENDOR_ID 0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
#define VIRTIO_MMIO_QUEUE_READY 0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064
#define VIRTIO_MMIO_STATUS 0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW 0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW 0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0a4
#define VIRTIO_MMIO_CONFIG_GENERATION 0x0fc
#define VIRTIO_MMIO_CONFIG 0x100
#define VIRTIO_MMIO_INT_VRING 0x01
#define VIRTIO_MMIO_INT_CONFIG 0x02

#define BIT(x) (1ULL << x)

#define offsetof(TYPE, MEMBER) ((size_t) & ((TYPE *)0)->MEMBER)

static int g_fuzz_iterations = 0;

union lkl_virtio_mutated_ring {
	struct {
		uint16_t mutated_avail_flags;
		uint16_t mutated_avail_idx;
		uint16_t mutated_avail_ring[VIRTIO_QUEUE_SIZE];

		uint16_t mutated_used_flags;
		uint16_t mutated_used_idx;
		uint32_t mutated_used_id[VIRTIO_QUEUE_SIZE];
		uint32_t mutated_used_len[VIRTIO_QUEUE_SIZE];

		uint16_t mutated_desc_next[VIRTIO_QUEUE_SIZE];
		uint16_t mutated_desc_flags[VIRTIO_QUEUE_SIZE];
		uint32_t mutated_desc_len[VIRTIO_QUEUE_SIZE];
		uint64_t mutated_desc_addr[VIRTIO_QUEUE_SIZE];
	} split;

	struct {
		struct lkl_vring_packed_desc desc[VIRTIO_QUEUE_SIZE];
	} packed;
};

struct fuzzer_data {
	uint8_t device_id;
	uint8_t vendor_id;
	// Indicate if want to mutate the virtio ring buffers before
	// the target code inserts descriptors in the queue.
	uint8_t mutate_queues_pre_insert;
	uint64_t device_features;
	union lkl_virtio_mutated_ring mutated_ring;
};

static struct fuzzer_data fuzz_data;

static int lkl_init()
{
	if (!get_fuzzer_log_enabled()) {
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
			set_fuzzer_log_enabled(false);
			break;
		}
	}

	lkl_init();

	__llvm_profile_initialize_file();
	atexit(flush_coverage);
	return 0;
}

static inline void virtio_deliver_irq(struct virtio_dev *dev)
{
	dev->int_status |= VIRTIO_MMIO_INT_VRING;
	/* Make sure all memory writes before are visible to the driver. */
	__sync_synchronize();
	lkl_trigger_irq(dev->irq);
}

// Virtio_ring in split mode uses 3 ring buffers which are readable/writable
// by host and the guest: desc, avail and used. The virtio front-end driver
// uses these buffers to:
//   1) insert requests into `desc` and `avail` buffers to pass them to the
//      back-end driver (for example, see `virtqueue_add_split` routine).
//   2) process completed requests from `desc` and `used` buffers once they
//      are handled by the back-end driver (for example, see `detach_buf_split`
//      routine)
//
// This fuzzer works by mutating contents of these 3 buffers before the
// front-end driver inserts the IO requests into the ring buffers (to
// trigger issues in the logic responsible for queueing the requests) and
// after the back-end driver (i.e. fuzzer) handles them (to trigger bugs
// in the virtio front-end driver code processing the completed requests).
static void mutate_split_ring_buffers(struct virtio_queue *q)
{
	size_t number_of_chained_desc = 0;

	q->split.avail->flags =
		fuzz_data.mutated_ring.split.mutated_avail_flags;
	q->split.avail->idx = fuzz_data.mutated_ring.split.mutated_avail_idx;

	q->split.used->flags = fuzz_data.mutated_ring.split.mutated_used_flags;
	q->split.used->idx = fuzz_data.mutated_ring.split.mutated_used_idx;

	// If q->split.used->idx haven't changed it will cause a hang as
	// the other end will still be waiting for the requests to
	// be completed. Thus, change it to a non-zero value.
	if (q->split.used->idx == 0)
		q->split.used->idx = 13;

	for (int i = 0; i < VIRTIO_QUEUE_SIZE; i++) {
		q->split.avail->ring[i] =
			fuzz_data.mutated_ring.split.mutated_avail_ring[i];

		q->split.used->ring[i].id =
			fuzz_data.mutated_ring.split.mutated_used_id[i];
		q->split.used->ring[i].len =
			fuzz_data.mutated_ring.split.mutated_used_len[i];

		q->split.desc[i].next =
			fuzz_data.mutated_ring.split.mutated_desc_next[i];
		q->split.desc[i].flags =
			fuzz_data.mutated_ring.split.mutated_desc_flags[i];
		q->split.desc[i].len =
			fuzz_data.mutated_ring.split.mutated_desc_len[i];
		q->split.desc[i].addr =
			fuzz_data.mutated_ring.split.mutated_desc_addr[i];

		if (q->split.desc[i].flags & LKL_VRING_DESC_F_NEXT)
			number_of_chained_desc++;
	}
	// TODO(b/225222112): In some cases if id field of an element in the ring
	// buffer isn't 0, it can result in the hang.
	// Implementing the workaround below for now.
	q->split.used->ring[0].id = 0;

	// If all the descriptors will be chained to each other, then the fuzzer
	// will enter in the infinite loop in `detach_buf_split`:
	//    while (vq->split.vring.desc[i].flags & nextflag) {
	//        vring_unmap_one_split(vq, i);
	//        i = vq->split.desc_extra[i].next;
	//        vq->vq.num_free++;
	//    }
	// Thus, we need to break this chain below. No particular reason for using
	// desc at `VIRTIO_QUEUE_SIZE - 2` index, feel free to change if needed.
	if (number_of_chained_desc == VIRTIO_QUEUE_SIZE)
		q->split.desc[VIRTIO_QUEUE_SIZE - 2].flags &=
			~LKL_VRING_DESC_F_NEXT;
}

// Virtio rings in packed mode organizes all the descriptors in a ring buffer.
// Both front-end and back-end drivers access those descriptors sequentially.
// Front-end driver fills in data and marks the descriptor "available". Backend
// driver will then find the available descriptors, process the request, and
// return the response by marking the descriptor "used".
// When mutating the ring buffer, we need to make sure we find the "available"
// buffer, and change them to "used" after we mutate the content. Otherwise the
// front-end driver will wait for that request forever and block the fuzzing
// progress. There is probably more we can fuzz other than just the "first"
// available buffer. This will require further understanding on how virtio ring
// works and analysis on the code coverage.
static void mutate_packed_ring_buffers(struct virtio_queue *q)
{
	uint16_t avail_idx = q->packed.last_avail_idx;
	uint16_t flags = q->packed.desc[avail_idx].flags;
	uint16_t id = q->packed.desc[avail_idx].id;

	bool avail = !!(flags & BIT(LKL_VRING_PACKED_DESC_F_AVAIL));
	bool used = !!(flags & BIT(LKL_VRING_PACKED_DESC_F_USED));

	if (avail == q->packed.ring_wrap_counter &&
	    used != q->packed.ring_wrap_counter) {
		flags ^= BIT(LKL_VRING_PACKED_DESC_F_USED);

		memcpy(q->packed.desc, fuzz_data.mutated_ring.packed.desc,
		       sizeof(fuzz_data.mutated_ring.packed.desc));

		// Due to how virtio system works, we need to supply the correct flags
		// and id to the descriptor, otherwise, the front end driver will wait
		// there forever, causing fuzzer timeout.
		q->packed.desc[avail_idx].flags = flags;
		q->packed.desc[avail_idx].id = id;

		q->packed.last_avail_idx++;
		if (q->packed.last_avail_idx == q->num) {
			q->packed.last_avail_idx = 0;
			q->packed.ring_wrap_counter ^= 1;
		}
	}
}

static void mutate_virtio_queue_ring_buffers(struct virtio_dev *dev,
					     uint32_t qidx)
{
	struct virtio_queue *q = &dev->queue[qidx];
	if (q->packed_ring) {
		mutate_packed_ring_buffers(q);
	} else {
		mutate_split_ring_buffers(q);
	}
}

static inline uint32_t virtio_read_device_features(struct virtio_dev *dev)
{
	if (dev->device_features_sel)
		return (uint32_t)(dev->device_features >> 32);

	return (uint32_t)dev->device_features;
}

static inline void virtio_write_driver_features(struct virtio_dev *dev,
						uint32_t val)
{
	uint64_t tmp;

	if (dev->driver_features_sel) {
		tmp = dev->driver_features & 0xFFFFFFFF;
		dev->driver_features = tmp | (uint64_t)val << 32;
	} else {
		tmp = dev->driver_features & 0xFFFFFFFF00000000;
		dev->driver_features = tmp | val;
	}
}

static int virtio_read(void *data, int offset, void *res, int size)
{
	uint32_t val;
	struct virtio_dev *dev = (struct virtio_dev *)data;

	LOG("virtio_read: offset %08X, size %08X\n", offset, size);

	if (offset >= VIRTIO_MMIO_CONFIG) {
		offset -= VIRTIO_MMIO_CONFIG;
		if (offset + size > dev->config_len)
			return -LKL_EINVAL;

		// Virtio configuration read request
		memcpy(res, dev->config_data + offset, size);
		return 0;
	}

	if (size != sizeof(uint32_t))
		return -LKL_EINVAL;

	switch (offset) {
	case VIRTIO_MMIO_MAGIC_VALUE:
		val = VIRTIO_DEV_MAGIC;
		break;
	case VIRTIO_MMIO_VERSION:
		val = VIRTIO_DEV_VERSION;
		break;
	case VIRTIO_MMIO_DEVICE_ID:
		val = dev->device_id;
		break;
	case VIRTIO_MMIO_VENDOR_ID:
		val = dev->vendor_id;
		break;
	case VIRTIO_MMIO_DEVICE_FEATURES:
		val = virtio_read_device_features(dev);
		break;
	case VIRTIO_MMIO_QUEUE_NUM_MAX:
		val = dev->queue[dev->queue_sel].num_max;
		break;
	case VIRTIO_MMIO_QUEUE_READY:
		val = dev->queue[dev->queue_sel].ready;
		break;
	case VIRTIO_MMIO_INTERRUPT_STATUS:
		val = dev->int_status;
		break;
	case VIRTIO_MMIO_STATUS:
		val = dev->status;
		break;
	case VIRTIO_MMIO_CONFIG_GENERATION:
		val = dev->config_gen;
		break;
	default:
		return -1;
	}

	*(uint32_t *)res = htole32(val);

	return 0;
}

static inline void set_ptr_low(void **ptr, uint32_t val)
{
	uint64_t tmp = (uintptr_t)*ptr;

	tmp = (tmp & 0xFFFFFFFF00000000) | val;
	*ptr = (void *)(long)tmp;
}

static inline void set_ptr_high(void **ptr, uint32_t val)
{
	uint64_t tmp = (uintptr_t)*ptr;

	tmp = (tmp & 0x00000000FFFFFFFF) | ((uint64_t)val << 32);
	*ptr = (void *)(long)tmp;
}

static inline void set_status(struct virtio_dev *dev, uint32_t val)
{
	if ((val & LKL_VIRTIO_CONFIG_S_FEATURES_OK) &&
	    (!(dev->driver_features & BIT(LKL_VIRTIO_F_VERSION_1)) ||
	     !(dev->driver_features & BIT(LKL_VIRTIO_RING_F_EVENT_IDX))))
		val &= ~LKL_VIRTIO_CONFIG_S_FEATURES_OK;
	dev->status = val;
}

static int virtio_write(void *data, int offset, void *res, int size)
{
	struct virtio_dev *dev = (struct virtio_dev *)data;
	struct virtio_queue *q = &dev->queue[dev->queue_sel];
	uint32_t val;
	int ret = 0;

	if (offset >= VIRTIO_MMIO_CONFIG) {
		offset -= VIRTIO_MMIO_CONFIG;

		LOG("virtio_write: offset %08X, size %08X\n", offset, size);
		if (offset + size >= dev->config_len)
			return -LKL_EINVAL;
		memcpy(dev->config_data + offset, res, size);
		return 0;
	}

	if (size != sizeof(uint32_t))
		return -LKL_EINVAL;

	val = le32toh(*(uint32_t *)res);
	LOG("virtio_write: offset %08X, size %08X, val %08X\n", offset, size,
	    val);

	switch (offset) {
	case VIRTIO_MMIO_QUEUE_NOTIFY:
		// The core of the fuzzer: at this point the front-end virtio driver
		// has added virtio requests into the ring buffers (i.e. updated
		// `desc` and `avail` buffers accordingly for the selected virtio
		// queue) and it signals the back-end driver to handle them.
		//
		// According to the virtio spec the write to VIRTIO_MMIO_QUEUE_NOTIFY
		// offset is used to signal the back-end virtio drivers by the front-end
		// drivers to start processing the virtio requests. Normally, once the
		// back-end driver finishes it should signal the front-end by raising
		// the IRQ (in case of asynchronous virtio handling such as implemented
		// in virtio_blk).
		//
		// Pretend that we handled all the virtio requests (i.e. read/wrote the
		// block device data) -- no op in the fuzzer.
		//
		// The legitimate virtio back-end driver would need to update `used`
		// shared buffer to indicate the number of requests processed by
		// the back-end from the `desc` buffer only. However, the fuzzer
		// mutates all the 3 buffers to test the front-end code.
		dump_queue_buffers(dev, val, VIRTIO_QUEUE_SIZE);
		mutate_virtio_queue_ring_buffers(dev, val);
		dump_queue_buffers(dev, val, VIRTIO_QUEUE_SIZE);
		// With all the shared buffers filled with the fuzz data we are ready
		// to signal the other side to indicate virtio queue has been finished
		// by the back-end so that the front-end code could process the
		// response.
		virtio_deliver_irq(dev);
		break;
	case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
		if (val > 1)
			return -LKL_EINVAL;
		dev->device_features_sel = val;
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
		if (val > 1)
			return -LKL_EINVAL;
		dev->driver_features_sel = val;
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES:
		virtio_write_driver_features(dev, val);
		break;
	case VIRTIO_MMIO_QUEUE_SEL:
		dev->queue_sel = val;
		break;
	case VIRTIO_MMIO_QUEUE_NUM:
		dev->queue[dev->queue_sel].num = val;
		break;
	case VIRTIO_MMIO_QUEUE_READY:
		dev->queue[dev->queue_sel].ready = val;
		if (val) {
			// This indicates the queue is initialized and ready to use. By
			// mutating the virtio buffers at this point, we test the code
			// which inserts requests into the ring buffers (i.e.
			// `virtqueue_add_split`).

			// Let the following code be executed in 50% of test cases.
			if (fuzz_data.mutate_queues_pre_insert & 1) {
				dump_queue_buffers(dev, dev->queue_sel /*qidx*/,
						   VIRTIO_QUEUE_SIZE);
				mutate_virtio_queue_ring_buffers(
					dev, dev->queue_sel);
				dump_queue_buffers(dev, dev->queue_sel /*qidx*/,
						   VIRTIO_QUEUE_SIZE);
			}
		}
		break;
	case VIRTIO_MMIO_INTERRUPT_ACK:
		dev->int_status = 0;
		break;
	case VIRTIO_MMIO_STATUS:
		set_status(dev, val);
		break;
	case VIRTIO_MMIO_QUEUE_DESC_LOW:
		if (dev->device_features & BIT(LKL_VIRTIO_F_RING_PACKED)) {
			set_ptr_low((void **)&q->packed.desc, val);
		} else {
			set_ptr_low((void **)&q->split.desc, val);
		}
		break;
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:
		if (dev->device_features & BIT(LKL_VIRTIO_F_RING_PACKED)) {
			q->packed_ring = true;
			q->packed.ring_wrap_counter = 1;
			set_ptr_high((void **)&q->packed.desc, val);
		} else {
			set_ptr_high((void **)&q->split.desc, val);
		}
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
		if (dev->device_features & BIT(LKL_VIRTIO_F_RING_PACKED)) {
			set_ptr_low((void **)&q->packed.device_event, val);
		} else {
			set_ptr_low((void **)&q->split.avail, val);
		}
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
		if (dev->device_features & BIT(LKL_VIRTIO_F_RING_PACKED)) {
			set_ptr_high((void **)&q->packed.device_event, val);
		} else {
			set_ptr_high((void **)&q->split.avail, val);
		}
		break;
	case VIRTIO_MMIO_QUEUE_USED_LOW:
		if (dev->device_features & BIT(LKL_VIRTIO_F_RING_PACKED)) {
			set_ptr_low((void **)&q->packed.driver_event, val);
		} else {
			set_ptr_low((void **)&q->split.used, val);
		}
		break;
	case VIRTIO_MMIO_QUEUE_USED_HIGH:
		if (dev->device_features & BIT(LKL_VIRTIO_F_RING_PACKED)) {
			set_ptr_high((void **)&q->packed.driver_event, val);
		} else {
			set_ptr_high((void **)&q->split.used, val);
		}
		break;
	default:
		ret = -1;
	}

	return ret;
}

static const struct lkl_iomem_ops virtio_ops = {
	.read = virtio_read,
	.write = virtio_write,
};

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	LOG("Begin fuzz iteration %08X, input size %08X.\n", g_fuzz_iterations,
	    Size);

	g_fuzz_iterations++;
	if (g_fuzz_iterations > 1000) {
		flush_coverage();
		g_fuzz_iterations = 0;
	}

	memset(&fuzz_data, 0x00, sizeof(fuzz_data));
	memcpy(&fuzz_data, Data,
	       Size < sizeof(fuzz_data) ? Size : sizeof(fuzz_data));

	union {
		struct lkl_virtio_blk_config blk;
	} config;

	memset(&config, 0x00, sizeof(config));

	struct virtio_dev dev;
	memset(&dev, 0x00, sizeof(dev));
	dev.device_id = fuzz_data.device_id;
	dev.vendor_id = fuzz_data.vendor_id;
	dev.device_features = fuzz_data.device_features;
	dev.config_gen = 0;
	dev.config_data = &config;

	if (dev.device_id == LKL_VIRTIO_ID_BLOCK) {
		// No specific reason for choosing the capacity below. We fuzz
		// virtio_blk config block in virtio_blk-fuzzer anyway. We just need
		// a reasonable size to let virtio_blk successfully probe the device.
		// Feel free to change to any other value if needed.
		config.blk.capacity = 1024 * 1024;
		dev.config_len = sizeof(config.blk);
	} else {
		dev.config_len = sizeof(config);
	}

	// don't need blk_ops for this fuzzer
	dev.ops = NULL;

	// Add different devices over virtio_mmio which will trigger initialization
	// of the device, which in turn will triggers transactions between frontend
	// and backend driver, over the virtio ring buffer. For example, with block
	// device, the following events will happen:
	//   1) an instance of virtio_mmio device whill be created to provide
	//      transport for virtio_blk (virtio_blk will bind to virtio_mmio)
	//   2) virtio_ring will create ring buffers for virtio_blk
	//   3) a new block device will be registered in the system
	//   4) the driver stack on top of the block device (e.g. filesystem)
	//      will attempt to read the first sector of the block device to
	//      determine the partitioning scheme
	//   5) step #4 will trigger creation of virtio requests to be sent to
	//      the back-end driver (i.e. this fuzzer) and at this time
	//      virtio_ring code will be fuzzed:
	//        a) by mutating ring buffers before the virtio front-end driver
	//           inserts the virtio requests into them
	//        b) by mutating ring buffers before sending the response back
	//           to the front-end driver.
	assert(virtio_dev_setup_ops(&dev, NUM_OF_VIRTIO_QUEUES,
				    VIRTIO_QUEUE_SIZE, &virtio_ops) == 0);
	assert(virtio_dev_cleanup(&dev) == 0);

	LOG("Done.\n");

	return 0;
}