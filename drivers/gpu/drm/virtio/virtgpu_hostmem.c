#include "virtgpu_drv.h"

static int virtgpu_hostmem_remap(struct virtio_gpu_hostmem_object *bo,
				 struct vm_area_struct *vma)
{
	int ret;
	unsigned long vm_size = vma->vm_end - vma->vm_start;

	/* Partial mappings of GEM buffers don't happen much in practice. */
	if (vm_size != bo->hostmem_node.size)
		return VM_FAULT_SIGBUS;

	ret = io_remap_pfn_range(vma, vma->vm_start,
				 bo->hostmem_node.start >> PAGE_SHIFT,
				 vm_size, vma->vm_page_prot);

	/* TODO: make atomic */
	if (ret == 0)
		bo->mapped++;

	return ret;
}

static void virtgpu_hostmem_vm_open(struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct virtio_gpu_hostmem_object *bo = gem_to_hostmem_obj(obj);

	bo->mapped++;
	drm_gem_vm_open(vma);
}

static void virtgpu_hostmem_vm_close(struct vm_area_struct *vma)
{
	struct virtio_gpu_object_array *objs;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct virtio_gpu_hostmem_object *bo = gem_to_hostmem_obj(obj);
	struct virtio_gpu_device *vgdev = obj->dev->dev_private;

	/* TODO: use proper context id */
	if (!--bo->mapped) {
		drm_mm_remove_node(&bo->hostmem_node);
		objs = virtio_gpu_array_alloc(1);
		virtio_gpu_array_add_obj(objs, obj);
		virtio_gpu_cmd_unmap(vgdev, 0, objs, NULL);
	}

	drm_gem_vm_close(vma);
}

static vm_fault_t virtgpu_hostmem_vm_fault(struct vm_fault *vmf)
{
	int ret;
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct virtio_gpu_hostmem_object *bo = gem_to_hostmem_obj(obj);

	if (!bo->hostmem_fence || bo->mapped) {
		DRM_ERROR("The buffer shouldn't be faulting");
		return VM_FAULT_SIGBUS;
	}

	ret = dma_fence_wait(&bo->hostmem_fence->f, true);
	if (ret)
		return ret;

	dma_fence_put(&bo->hostmem_fence->f);
	bo->hostmem_fence = NULL;

	return virtgpu_hostmem_remap(bo, vma);
}

static const struct vm_operations_struct virtgpu_hostmem_vm_ops = {
	.open = virtgpu_hostmem_vm_open,
	.close = virtgpu_hostmem_vm_close,
	.fault = virtgpu_hostmem_vm_fault,
};

int virtio_gpu_hostmem_map(struct drm_gem_object *obj)
{
	int ret;
    uint64_t offset;
	struct virtio_gpu_object_array *objs;
	struct virtio_gpu_device *vgdev = obj->dev->dev_private;
	struct virtio_gpu_hostmem_object *bo = gem_to_hostmem_obj(obj);

	if (!vgdev->has_host_visible)
		return -EINVAL;

	/* Only bother allocating address space when we map the buffer. */
	ret = drm_mm_insert_node(&vgdev->host_visible_mm, &bo->hostmem_node,
				  obj->size);
	if (ret) {
		drm_mm_remove_node(&bo->hostmem_node);
		return ret;
	}

	objs = virtio_gpu_array_alloc(1);
	if (!objs) {
		drm_mm_remove_node(&bo->hostmem_node);
		return -ENOMEM;
	}

	virtio_gpu_array_add_obj(objs, obj);
	bo->hostmem_fence = virtio_gpu_fence_alloc(vgdev);
	if (!bo->hostmem_fence) {
		drm_mm_remove_node(&bo->hostmem_node);
		virtio_gpu_array_put_free(objs);
		return -ENOMEM;
	}

    offset = bo->hostmem_node.start - vgdev->hostmem.addr;
	/* TODO: use proper context id */
	virtio_gpu_cmd_map(vgdev, 0, objs, offset, bo->hostmem_fence);

	return 0;
}

static void virtio_gpu_hostmem_free(struct drm_gem_object *obj)
{
	drm_gem_free_mmap_offset(obj);
	drm_gem_object_release(obj);
	kfree(obj);
}

static int virtio_gpu_hostmem_open(struct drm_gem_object *obj,
				   struct drm_file *file)
{
	return 0;
}

static void virtio_gpu_hostmem_close(struct drm_gem_object *obj,
				     struct drm_file *file)
{

}

static int virtio_gpu_hostmem_mmap(struct drm_gem_object *obj,
				   struct vm_area_struct *vma)
{
	struct virtio_gpu_hostmem_object *bo = gem_to_hostmem_obj(obj);
	/* Clear fake offset. */
	vma->vm_pgoff -= drm_vma_node_start(&obj->vma_node);

	vma->vm_flags |= VM_MIXEDMAP | VM_DONTEXPAND;
	/* TODO: tracking caching somewhere */
	vma->vm_page_prot =
		pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);
	vma->vm_ops = &virtgpu_hostmem_vm_ops;

	/* TODO: make atomic */
	if (bo->mapped || bo->hostmem_fence)
		return virtgpu_hostmem_remap(bo, vma);

	virtio_gpu_hostmem_map(obj);
	return 0;
}

static const struct drm_gem_object_funcs virtio_gpu_hostmem_funcs = {
	.open = virtio_gpu_hostmem_open,
	.close = virtio_gpu_hostmem_close,
	.free = virtio_gpu_hostmem_free,

	.mmap = virtio_gpu_hostmem_mmap,
};

bool virtio_gpu_is_hostmem_obj(struct drm_gem_object *obj)
{
	return obj->funcs == &virtio_gpu_hostmem_funcs;
}

struct drm_gem_object *virtio_gpu_hostmem_create(struct drm_device *dev,
						 uint32_t flags,
						 size_t size)
{
	struct drm_gem_object *obj;
	struct virtio_gpu_hostmem_object *bo;
	struct virtio_gpu_device *vgdev = dev->dev_private;
	int ret;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return NULL;
	obj = &bo->base;
	obj->funcs = &virtio_gpu_hostmem_funcs;

	drm_gem_private_object_init(dev, obj, size);

	/* Create fake offset */
	ret = drm_gem_create_mmap_offset(obj);
	if (ret) {
		kfree(bo);
		return NULL;
	}

	ret = virtio_gpu_resource_id_get(vgdev, &bo->hw_res_handle);
		DRM_ERROR("bo->hw_res_handle: %p %u\n", bo, bo->hw_res_handle);
	if (ret) {
		DRM_ERROR("free bo\n");
		kfree(bo);
		return NULL;
	}

	return &bo->base;
}
