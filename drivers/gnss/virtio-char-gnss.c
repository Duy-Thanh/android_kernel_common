// SPDX-License-Identifier: GPL-2.0
/*
 * virtio serial driver for GNSS. This driver requires the serdev binding
 *
 * Copyright 2021 Google LLC
 */

#include <linux/device.h>
#include <linux/gnss.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serdev.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/virtio.h>
#include <linux/virtio_console.h>

#include "serial.h"

struct virtio_char_gnss_dev {
    struct virtio_device *vdev;
	struct cdev *cdev;
	struct device *dev;
	struct virtqueue *in_vq, *out_vq;
    int chr_major;
    u32 id;
};

static struct class *myclass;
static struct virtio_char_gnss_dev* mydev;
static const unsigned int features[] = {
};

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_CHAR_GNSS, VIRTIO_DEV_ANY_ID },
	{ 0 },
};
MODULE_DEVICE_TABLE(virtio, id_table);

static const struct file_operations gnss_char_fops = {
	.owner = THIS_MODULE,
/*
	.open  = gnss_char_fops_open,
	.read  = gnss_char_fops_read,
	.write = gnss_char_fops_write,
	.poll  = gnss_char_fops_poll,
	.release = gnss_char_fops_release,
*/
};

static int virtio_gnss_probe(struct virtio_device *vdev)
{

    mydev->vdev = vdev;
    vdev->priv = mydev;

    dev_err(&vdev->dev, "calling %s %d here\n", __func__, __LINE__);

    return 0;
}

static void virtio_gnss_remove(struct virtio_device *vdev)
{
}

#ifdef CONFIG_PM_SLEEP
static int virtio_gnss_freeze(struct virtio_device *vdev)
{
    return 0;
}
static int virtio_gnss_restore(struct virtio_device *vdev)
{
    return 0;
}
#endif

static struct virtio_driver virtio_char_gnss_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virtio_gnss_probe,
	.remove =	virtio_gnss_remove,
#ifdef CONFIG_PM_SLEEP
	.freeze =	virtio_gnss_freeze,
	.restore =	virtio_gnss_restore,
#endif
};

static int __init gnss_virtio_init(void)
{

	dev_t devt;

    myclass = class_create(THIS_MODULE, "virtio-gnss-char-class");

    mydev = kmalloc(sizeof(*mydev), GFP_KERNEL);
    mydev->chr_major = register_chrdev(0, "virtio-gnss-char-dev",
					     &gnss_char_fops);
    mydev->cdev = cdev_alloc();
    mydev->cdev->owner = THIS_MODULE;
    mydev->id = 0;
    devt = MKDEV(mydev->chr_major, mydev->id);
    mydev->cdev->ops = &gnss_char_fops;

    mydev->dev = device_create(myclass, NULL, devt, mydev, "virtio-gnss-char%u", mydev->id);
	cdev_add(mydev->cdev, devt, 1);


    register_virtio_driver(&virtio_char_gnss_driver);
	return 0;
}

static void __exit gnss_virtio_exit(void)
{

    kfree(mydev);
}

module_init(gnss_virtio_init);
module_exit(gnss_virtio_exit);

MODULE_AUTHOR("Bo Hu <bohu@google.com>");
MODULE_DESCRIPTION("GNSS virtio serial driver");
MODULE_SOFTDEP("pre: gnss_serial");
MODULE_SOFTDEP("pre: virtio_console");
MODULE_SOFTDEP("pre: virtio_pci");
MODULE_LICENSE("GPL v2");
