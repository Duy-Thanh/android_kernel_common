// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/fcntl.h>
#include <linux/ioctl.h>
#include <linux/sizes.h>
#include <linux/videodev2.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>

#include "../../ppps/ppps_misc_module.h"

#define TEST_PLANE_SIZE (6 * SZ_1K)
#define VB2_MMAP_PPPS_EXPBUF _IO('v', 0x70)

static DEFINE_MUTEX(test_queue_lock);
static struct vb2_queue test_queue;

static int test_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
			    unsigned int *num_planes, unsigned int sizes[],
			    struct device *alloc_devs[])
{
	if (*num_planes) {
		if (sizes[0] < TEST_PLANE_SIZE)
			return -EINVAL;
		return 0;
	}

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

static int test_mmap(struct file *file, struct vm_area_struct *vma)
{
	return vb2_mmap(&test_queue, vma);
}

static long test_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vb2_buffer *vb = test_queue.bufs[0];
	int fd;
	int ret;

	if (cmd != VB2_MMAP_PPPS_EXPBUF)
		return -ENOTTY;
	if (!vb)
		return -ENODEV;

	ret = vb2_core_expbuf(&test_queue, &fd, test_queue.type, vb, 0,
			      O_RDWR);
	return ret ? ret : fd;
}

static const struct file_operations test_fops = {
	.owner = THIS_MODULE,
	.mmap = test_mmap,
	.unlocked_ioctl = test_ioctl,
};

static int test_setup(void)
{
	unsigned int count = 1;
	int ret;

	test_queue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	test_queue.io_modes = VB2_MMAP;
	test_queue.ops = &test_queue_ops;
	test_queue.mem_ops = &vb2_vmalloc_memops;
	test_queue.lock = &test_queue_lock;

	ret = vb2_core_queue_init(&test_queue);
	if (ret)
		return ret;
	ret = vb2_core_reqbufs(&test_queue, VB2_MEMORY_MMAP, 0, &count);
	if (ret)
		vb2_core_queue_release(&test_queue);
	return ret;
}

static void test_teardown(void)
{
	vb2_core_queue_release(&test_queue);
}

PPPS_MISC_MODULE("vb2_mmap_ppps", &test_fops, 0600, test_setup, test_teardown,
		 "videobuf2 mmap PPPS regression fixture");
