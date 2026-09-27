// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for the Cleancache.
 *
 * Copyright (C) 2026, Google LLC.
 * Author: Suren Baghdasaryan <surenb@google.com>
 */
#include <kunit/device.h>
#include <kunit/test.h>

#include <linux/dma-buf.h>
#include <linux/fdtable.h>
#include <linux/mman.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/wrapfd.h>

struct test_buffer {
    void *vaddr;
    size_t size;
    struct sg_table sgt;
};

static struct sg_table *test_map_dmabuf(struct dma_buf_attachment *attach,
		enum dma_data_direction direction) {
	struct test_buffer *priv = attach->dmabuf->priv;

	return &priv->sgt;
}

static void test_unmap_dmabuf(struct dma_buf_attachment *attach,
		struct sg_table *sgt, enum dma_data_direction direction) {
	/* No-op for dummy testing memory */
}

static void test_release(struct dma_buf *dmabuf) {
	struct test_buffer *priv = dmabuf->priv;

	sg_free_table(&priv->sgt);
	vfree(priv->vaddr);
	kfree(priv);
}

/* Minimal operations table required by dma_buf_export() */
static const struct dma_buf_ops test_dmabuf_ops = {
	.map_dma_buf   = test_map_dmabuf,
	.unmap_dma_buf = test_unmap_dmabuf,
	.release       = test_release,
};

static struct dma_buf *create_test_dmabuf(size_t size) {
	struct test_buffer *priv;
	struct dma_buf *dmabuf;
	struct page *page;
	int ret;

	size = PAGE_ALIGN(size);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->size = size;

	priv->vaddr = vmalloc(size);
	if (!priv->vaddr) {
		kfree(priv);
		return ERR_PTR(-ENOMEM);
	}

	ret = sg_alloc_table(&priv->sgt, 1, GFP_KERNEL);
	if (ret) {
		vfree(priv->vaddr);
		kfree(priv);
		return ERR_PTR(ret);
	}

	page = vmalloc_to_page(priv->vaddr);
	sg_set_page(priv->sgt.sgl, page, size, 0);

	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	exp_info.ops = &test_dmabuf_ops;
	exp_info.size = size;
	exp_info.flags = O_RDWR;
	exp_info.priv = priv;

	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		sg_free_table(&priv->sgt);
		vfree(priv->vaddr);
		kfree(priv);
	}

	return dmabuf;
}

static struct test_data {
	struct device *mock_dev1;
	struct device *mock_dev2;
	struct dma_buf *dmabuf;
	int empty_wrapfd;
	int wrapfd;
	bool emptied;
	struct file *file;
	struct file *empty_file;
} test_data;

static int wrapfd_suite_init(struct kunit_suite *suite)
{
	struct dma_buf *dmabuf;
	struct file *file;
	int empty_wrapfd;
	int dmabuf_fd;
	int wrapfd;
	int err;

	memset(&test_data, 0, sizeof(test_data));

	dmabuf = create_test_dmabuf(10 * PAGE_SIZE);
	if (IS_ERR(dmabuf)) {
		err = PTR_ERR(dmabuf);
		pr_err("Failed to create DMA-BUF: %d\n", err);
		return err;
	}

	dmabuf_fd = dma_buf_fd(dmabuf, O_CLOEXEC);
	if (dmabuf_fd < 0) {
		err = dmabuf_fd;
		pr_err("Failed to install dmabuf fd: %d\n", err);
		goto dmabuf_put;
	}

	/*
	 * Can't empty now because device to take the ownership is not ready yet.
	 * Will do that after devices are initialized in wrapfd_test_init().
	 */
	empty_wrapfd = create_wrapfd(dmabuf_fd, PROT_READ | PROT_WRITE);
	if (empty_wrapfd < 0) {
		err = empty_wrapfd;
		pr_err("Failed to create wrapfd: %d\n", err);
		goto close_dmabuf;
	}

	wrapfd = create_wrapfd(dmabuf_fd, PROT_READ | PROT_WRITE);
	if (wrapfd < 0) {
		err = wrapfd;
		pr_err("Failed to create wrapfd: %d\n", err);
		goto close_empty_wrapfd;
	}
	close_fd(dmabuf_fd);

	test_data.dmabuf = dmabuf;
	test_data.wrapfd = wrapfd;
	test_data.empty_wrapfd = empty_wrapfd;
	test_data.file = fget(wrapfd);
	test_data.empty_file = fget(empty_wrapfd);

	return 0;
close_empty_wrapfd:
	file = file_close_fd(empty_wrapfd);
	fput(file);
close_dmabuf:
	close_fd(dmabuf_fd);
dmabuf_put:
	dma_buf_put(dmabuf);
	return err;
}

static void wrapfd_suite_exit(struct kunit_suite *suite)
{
	struct file *file;

	if (!test_data.dmabuf)
		return;

	file = file_close_fd(test_data.empty_wrapfd);
	fput(file);
	fput(test_data.file);

	file = file_close_fd(test_data.wrapfd);
	fput(file);
	fput(test_data.empty_file);
}

static int wrapfd_test_init(struct kunit *test)
{
	struct device *dev1;
	struct device *dev2;

	if (!test_data.dmabuf)
		kunit_skip(test, "DMA-BUF was not created");

	dev1 = kunit_device_register(test, "mock_device1");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev1);
	test_data.mock_dev1 = dev1;

	dev2 = kunit_device_register(test, "mock_device2");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev2);
	test_data.mock_dev2 = dev2;

	if (!test_data.emptied) {
		KUNIT_ASSERT_EQ(test, wrapfd_get_ownership(test_data.empty_file, dev1), 0);
		KUNIT_ASSERT_EQ(test, empty_wrapfd(test_data.empty_file, dev1), 0);
		KUNIT_ASSERT_EQ(test, wrapfd_put_ownership(test_data.empty_file, dev1), 0);
		test_data.emptied = true;
	}

	return 0;
}

static void wrapfd_test_exit(struct kunit *test)
{
	if (test_data.mock_dev2) {
		kunit_device_unregister(test, test_data.mock_dev2);
		test_data.mock_dev2 = NULL;
	}
	if (test_data.mock_dev1) {
		kunit_device_unregister(test, test_data.mock_dev1);
		test_data.mock_dev1 = NULL;
	}
}

static void wrapfd_get_mappable_test(struct kunit *test)
{
	struct device *dev1 = test_data.mock_dev1;
	struct device *dev2 = test_data.mock_dev2;
	union wrapfd_mappable mappable;
	struct file *empty_file;
	struct file *file;

	empty_file = test_data.empty_file;
	file = test_data.file;

	/* Test invalid inputs */
	KUNIT_ASSERT_EQ(test, wrapfd_get_mappable(NULL, dev1, &mappable), -EBADF);
	KUNIT_ASSERT_EQ(test, wrapfd_put_mappable(NULL, dev1, &mappable), -EBADF);
	KUNIT_ASSERT_EQ(test, wrapfd_get_mappable(file, NULL, &mappable), -ENODEV);
	KUNIT_ASSERT_EQ(test, wrapfd_put_mappable(file, NULL, &mappable), -ENODEV);
	KUNIT_ASSERT_EQ(test, wrapfd_get_mappable(file, dev1, NULL), -EINVAL);
	KUNIT_ASSERT_EQ(test, wrapfd_put_mappable(file, dev1, NULL), -EINVAL);

	/* Get mappable object when no owners and no other users */
	KUNIT_ASSERT_EQ(test, wrapfd_get_mappable(file, dev1, &mappable), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_put_mappable(file, dev1, &mappable), 0);

	/* Get mappable object with other users */
	KUNIT_ASSERT_EQ(test, wrapfd_get_mappable(file, dev1, &mappable), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_get_mappable(file, dev2, &mappable), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_put_mappable(file, dev1, &mappable), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_put_mappable(file, dev2, &mappable), 0);

	/* Get mappable object while being the owner */
	KUNIT_ASSERT_EQ(test, wrapfd_get_ownership(file, dev1), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_get_mappable(file, dev1, &mappable), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_put_mappable(file, dev1, &mappable), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_put_ownership(file, dev1), 0);

	/* Get mappable object while someone else is the owner */
	KUNIT_ASSERT_EQ(test, wrapfd_get_ownership(file, dev2), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_get_mappable(file, dev1, &mappable), -EBUSY);
	KUNIT_ASSERT_EQ(test, wrapfd_put_ownership(file, dev2), 0);

	/* Get mappable object of an empty buffer */
	KUNIT_ASSERT_EQ(test, wrapfd_get_mappable(empty_file, dev1, &mappable),
					-ENOENT);
}

static void wrapfd_get_ownership_test(struct kunit *test)
{
	struct device *dev1 = test_data.mock_dev1;
	struct device *dev2 = test_data.mock_dev2;
	union wrapfd_mappable mappable;
	struct file *empty_file;
	struct file *file;

	empty_file = test_data.empty_file;
	file = test_data.file;

	/* Test invalid inputs */
	KUNIT_ASSERT_EQ(test, wrapfd_get_ownership(NULL, dev1), -EBADF);
	KUNIT_ASSERT_EQ(test, wrapfd_put_ownership(NULL, dev1), -EBADF);
	KUNIT_ASSERT_EQ(test, wrapfd_get_ownership(file, NULL), -ENODEV);
	KUNIT_ASSERT_EQ(test, wrapfd_put_ownership(file, NULL), -ENODEV);

	/* Get ownership when no owners and no other users */
	KUNIT_ASSERT_EQ(test, wrapfd_get_ownership(file, dev1), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_put_ownership(file, dev1), 0);

	/* Get ownership while owning the buffer */
	KUNIT_ASSERT_EQ(test, wrapfd_get_ownership(file, dev1), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_get_ownership(file, dev1), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_put_ownership(file, dev1), 0);

	/* Get ownership while someone else owns the buffer */
	KUNIT_ASSERT_EQ(test, wrapfd_get_ownership(file, dev2), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_get_ownership(file, dev1), -EBUSY);
	KUNIT_ASSERT_EQ(test, wrapfd_put_ownership(file, dev2), 0);

	/* Get ownership while mapping the buffer */
	KUNIT_ASSERT_EQ(test, wrapfd_get_mappable(file, dev1, &mappable), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_get_ownership(file, dev1), -EINVAL);
	KUNIT_ASSERT_EQ(test, wrapfd_put_mappable(file, dev1, &mappable), 0);

	/* Get ownership while someone else maps the buffer */
	KUNIT_ASSERT_EQ(test, wrapfd_get_mappable(file, dev2, &mappable), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_get_ownership(file, dev1), -EINVAL);
	KUNIT_ASSERT_EQ(test, wrapfd_put_mappable(file, dev2, &mappable), 0);

	/* Put ownership when nobody owns the buffer */
	KUNIT_ASSERT_EQ(test, wrapfd_put_ownership(file, dev1), -EBUSY);

	/* Put ownership when someone else owns the buffer */
	KUNIT_ASSERT_EQ(test, wrapfd_get_ownership(file, dev2), 0);
	KUNIT_ASSERT_EQ(test, wrapfd_put_ownership(file, dev1), -EBUSY);
	KUNIT_ASSERT_EQ(test, wrapfd_put_ownership(file, dev2), 0);

	/* Get ownership of an empty buffer */
	KUNIT_ASSERT_EQ(test, wrapfd_get_ownership(empty_file, dev2), -ENOENT);
}

static struct kunit_case wrapfd_test_cases[] = {
	KUNIT_CASE(wrapfd_get_mappable_test),
	KUNIT_CASE(wrapfd_get_ownership_test),
	{},
};

static struct kunit_suite wrapfd_test_module = {
	.name = "wrapfd",
	.init = wrapfd_test_init,
	.exit = wrapfd_test_exit,
	.suite_init = wrapfd_suite_init,
	.suite_exit = wrapfd_suite_exit,
	.test_cases = wrapfd_test_cases,
};

kunit_test_suites(&wrapfd_test_module);

MODULE_DESCRIPTION("KUnit test for the wrapfd driver");
MODULE_LICENSE("GPL v2");
