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

#include "log.h"

static bool g_log_enabled = true;

bool get_fuzzer_log_enabled()
{
	return g_log_enabled;
}
void set_fuzzer_log_enabled(bool enabled)
{
	g_log_enabled = enabled;
}

static void dump_desc_buffer_split(struct lkl_vring_desc *desc, int queue_index,
				   int queue_size)
{
	LOG("Dumping desc buffer for split queue #%d:\n", queue_index);

	for (int i = 0; i < queue_size; i++) {
		LOG("desc #%X: addr %p, len %08X, flags %04X, next %04X\n", i,
		    desc[i].addr, desc[i].len, desc[i].flags, desc[i].next);
	}
}

static void dump_avail_buffer_split(struct lkl_vring_avail *avail,
				    int queue_index, int queue_size)
{
	LOG("Dumping avail buffer for split queue #%d:\n", queue_index);
	LOG("flags %04X, idx %X\n", avail->flags, avail->idx);
	LOG("ring[]:\n");

	for (int i = 0; i < queue_size; i++) {
		LOG("%X, ", avail->ring[i]);
	}

	LOG("\n");
}

static void dump_used_buffer_split(struct lkl_vring_used *used, int queue_index,
				   int queue_size)
{
	LOG("Dumping used buffer for split queue #%d:\n", queue_index);
	LOG("flags %04X, idx %X\n", used->flags, used->idx);
	LOG("ring[]:\n");

	for (int i = 0; i < queue_size; i++) {
		LOG("(%X, %X), ", used->ring[i].id, used->ring[i].len);
	}

	LOG("\n");
}

static void dump_desc_buffer_packed(struct lkl_vring_packed_desc *desc,
				    int queue_index, int queue_size)
{
	LOG("Dumping desc buffer for packed queue #%d:\n", queue_index);

	for (int i = 0; i < queue_size; i++) {
		LOG("desc #%X: addr %p, len %08X, flags %04X, id %04X\n", i,
		    desc[i].addr, desc[i].len, desc[i].flags, desc[i].id);
	}
}

void dump_queue_buffers(struct virtio_dev *dev, uint32_t qidx, int queue_size)
{
	if (unlikely(get_fuzzer_log_enabled())) {
		struct virtio_queue *q = &dev->queue[qidx];
		if (q->packed_ring) {
			dump_desc_buffer_packed(q->packed.desc, qidx,
						queue_size);
		} else {
			dump_desc_buffer_split(q->split.desc, qidx, queue_size);
			dump_avail_buffer_split(q->split.avail, qidx,
						queue_size);
			dump_used_buffer_split(q->split.used, qidx, queue_size);
		}
	}
}