/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LKL_VIRTIO_RING_FUZZER_LOG_H
#define _LKL_VIRTIO_RING_FUZZER_LOG_H

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

bool get_fuzzer_log_enabled();
void set_fuzzer_log_enabled(bool enabled);

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define LOG(fmt, ...)                                                          \
	if (unlikely(get_fuzzer_log_enabled())) {                              \
		printf(fmt, ##__VA_ARGS__);                                    \
	}

void dump_queue_buffers(struct virtio_dev *dev, uint32_t qidx, int queue_size);

#endif // _LKL_VIRTIO_RING_FUZZER_LOG_H