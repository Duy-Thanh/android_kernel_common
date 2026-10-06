// SPDX-License-Identifier: GPL-2.0

#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>

#include "../../ppps/ppps_misc_module.h"

#define TEST_PLANE_SIZE 4096
#define VB2_USERPTR_REJECT_PPPS_RUN _IO('v', 0x71)

static DEFINE_MUTEX(test_queue_lock);

static int test_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
			    unsigned int *num_planes, unsigned int sizes[],
			    struct device *alloc_devs[])
{
	*num_planes = 1;
	sizes[0] = TEST_PLANE_SIZE;
	return 0;
}

static void test_buf_queue(struct vb2_buffer *vb)
{
}

static const struct vb2_ops test_queue_ops = {
	.queue_setup = test_queue_setup,
	.buf_queue = test_buf_queue,
};

static long test_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vb2_queue q = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.io_modes = VB2_USERPTR,
		.ops = &test_queue_ops,
		.mem_ops = &vb2_vmalloc_memops,
		.lock = &test_queue_lock,
	};
	unsigned int count = 1;
	int ret;

	if (cmd != VB2_USERPTR_REJECT_PPPS_RUN)
		return -ENOTTY;
	ret = vb2_core_queue_init(&q);
	if (ret)
		return ret;
	ret = vb2_core_reqbufs(&q, VB2_MEMORY_USERPTR, 0, &count);
	vb2_core_queue_release(&q);
	return ret;
}

static const struct file_operations test_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = test_ioctl,
};

PPPS_MISC_MODULE("vb2_userptr_reject_ppps", &test_fops, 0600,
		 ppps_misc_no_setup, ppps_misc_no_teardown,
		 "videobuf2 USERPTR rejection regression fixture");
