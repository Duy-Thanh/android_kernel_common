// SPDX-License-Identifier: GPL-2.0

#include <linux/build_bug.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <media/frame_vector.h>

#include "../frame_vector_ppps.h"

#include "../../ppps/ppps_misc_module.h"

/* Keep the GKI-visible layout recorded in gki/aarch64/abi.stg. */
static_assert(offsetof(struct frame_vector, got_ref) == 8);
static_assert(offsetof(struct frame_vector, is_pfns) == 9);
static_assert(offsetof(struct frame_vector, ptrs) == 16);
static_assert(sizeof(struct frame_vector) == 16);

static long frame_vector_ppps_ioctl(struct file *file, unsigned int cmd,
				    unsigned long arg)
{
	struct frame_vector_ppps_args request;
	struct frame_vector *vec;
	unsigned int i;
	int ret;

	if (cmd != FRAME_VECTOR_PPPS_IOCTL)
		return -EINVAL;
	if (copy_from_user(&request, (void __user *)arg, sizeof(request)))
		return -EFAULT;
	if (!request.capacity ||
	    request.capacity > FRAME_VECTOR_PPPS_MAX_FRAMES ||
	    request.nr_frames > request.capacity)
		return -EINVAL;

	vec = frame_vector_create(request.capacity);
	if (!vec)
		return -ENOMEM;
	ret = get_vaddr_frames(request.address, request.nr_frames,
			       request.write, vec);
	request.result = ret;
	request.count = frame_vector_count(vec);
	request.frame_size = frame_vector_frame_size(vec);
	memset(request.offsets, 0, sizeof(request.offsets));
	for (i = 0; i < request.count; i++)
		request.offsets[i] = frame_vector_frame_offset(vec, i);
	if (ret > 0)
		put_vaddr_frames(vec);
	frame_vector_destroy(vec);

	if (copy_to_user((void __user *)arg, &request, sizeof(request)))
		return -EFAULT;
	return 0;
}

static const struct file_operations frame_vector_ppps_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = frame_vector_ppps_ioctl,
	.compat_ioctl = frame_vector_ppps_ioctl,
};

PPPS_MISC_MODULE(FRAME_VECTOR_PPPS_DEVICE_NAME, &frame_vector_ppps_fops, 0,
		 ppps_misc_no_setup, ppps_misc_no_teardown,
		 "frame_vector ABI and legacy pinning regression fixture");
