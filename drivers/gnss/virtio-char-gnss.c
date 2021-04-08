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
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/virtio.h>
#include <linux/virtio_console.h>

#include "serial.h"

#define GNSS_VQ_RX 0
#define GNSS_VQ_TX 1
#define GNSS_NUM_VQ 2

struct virtio_char_gnss_dev {
    struct virtio_device *vdev;
	struct device *dev;
    struct gnss_device * gdev;
    struct virtqueue *gnss_vqs[GNSS_NUM_VQ];
    bool enable;
};


static struct virtio_char_gnss_dev* s_gnss_vdev;
static const unsigned int features[] = {
};

struct gnss_buf {
	char *buf;

	/* size of the buffer in *buf above */
	size_t size;

};

static struct gnss_buf *alloc_buf(size_t buf_size) {
    struct gnss_buf *gbuf;

    gbuf = kmalloc(sizeof(*gbuf), GFP_KERNEL);
    gbuf->buf = kmalloc(buf_size, GFP_KERNEL);
    gbuf->size = buf_size;
    return gbuf;
}

static void free_buf(struct gnss_buf* gbuf) {
    kfree(gbuf->buf);
    kfree(gbuf);
}


static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_GNSS, VIRTIO_DEV_ANY_ID },
	{ 0 },
};
MODULE_DEVICE_TABLE(virtio, id_table);

static int gnss_tx_virtio(struct gnss_buf *gbuf)
{
	struct scatterlist sg[1];
	int err;

	sg_init_one(sg, gbuf->buf, gbuf->size);
	err = virtqueue_add_outbuf(s_gnss_vdev->gnss_vqs[GNSS_VQ_TX], sg, 1, gbuf, GFP_ATOMIC);
	if (err) {
		return err;
    }
	virtqueue_kick(s_gnss_vdev->gnss_vqs[GNSS_VQ_TX]);
	return 0;
}

static void start_read() {
	struct scatterlist sg;

    int count;
    char *buf;
    bool success;

    if (!s_gnss_vdev->enable) {
        return;
    }

    buf = kmalloc(1024, GFP_KERNEL);
    size_t size = 1024;

	sg_init_one(&sg, buf, size);

	/* There should always be room for one buffer. */
	virtqueue_add_inbuf(s_gnss_vdev->gnss_vqs[GNSS_VQ_RX], &sg, 1, buf, GFP_KERNEL);

	while(!(success = virtqueue_kick(s_gnss_vdev->gnss_vqs[GNSS_VQ_RX]))) {
    }
}


static void gnss_virtio_rx_done(struct virtqueue *vq) {
    char *buf;
    int ret;
    unsigned int data_avail;
	if (!(buf=virtqueue_get_buf(vq, &data_avail)))
		return;

    ret = gnss_insert_raw(s_gnss_vdev->gdev, buf, data_avail);

    kfree(buf);

    if (ret > 0) {
        // ready for more data
        start_read();
    } else {
        // buffer is full, stop it
        s_gnss_vdev->enable = false;
    }
}

static void gnss_virtio_tx_done(struct virtqueue *vq)
{
	unsigned int len;
	struct gnss_buf *gbuf;

	while ((gbuf = virtqueue_get_buf(vq, &len))) {
        free_buf(gbuf);
    }
}

static int init_vqs(struct virtio_device *vdev)
{
	vq_callback_t *callbacks[GNSS_NUM_VQ] = {
		[GNSS_VQ_RX] = gnss_virtio_rx_done,
		[GNSS_VQ_TX] = gnss_virtio_tx_done,
	};
	const char *names[GNSS_NUM_VQ] = {
		[GNSS_VQ_RX] = "input",
		[GNSS_VQ_TX] = "output",
	};

	return virtio_find_vqs(vdev, GNSS_NUM_VQ,
			       s_gnss_vdev->gnss_vqs, callbacks, names, NULL);
}

/* The host will fill any buffer we give it with data. */
static void register_buffer(char *buf, size_t size)
{
	struct scatterlist sg;

	sg_init_one(&sg, buf, size);

	/* There should always be room for one buffer. */
	virtqueue_add_inbuf(s_gnss_vdev->gnss_vqs[GNSS_VQ_RX], &sg, 1, buf, GFP_KERNEL);

	virtqueue_kick(s_gnss_vdev->gnss_vqs[GNSS_VQ_RX]);
}

static int virtio_gnss_probe(struct virtio_device *vdev)
{
    s_gnss_vdev->vdev = vdev;
    vdev->priv = s_gnss_vdev;

    init_vqs(vdev);

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

static int char_open(struct gnss_device *gdev)
{
    s_gnss_vdev->enable = true;
    start_read();
	return 0;

}


static void char_close(struct gnss_device *gdev)
{
    s_gnss_vdev->enable = false;
}

static int char_write_raw(struct gnss_device *gdev,
		const unsigned char *buf, size_t count)
{
    struct gnss_buf *gbuf;

    gbuf = alloc_buf(count);

    memcpy(gbuf->buf, buf, count);

    gnss_tx_virtio(gbuf);

	return count;
}



static const struct gnss_operations my_ops = {
	.open		= char_open,
	.close		= char_close,
	.write_raw	= char_write_raw,
};


static void register_gnss_device() {
    struct gnss_device* gdev;

    gdev = gnss_allocate_device(s_gnss_vdev->dev);

	gdev->ops = &my_ops;
	gnss_set_drvdata(gdev, s_gnss_vdev);

    s_gnss_vdev->gdev = gdev;

   gnss_register_device(gdev);

}

static int __init gnss_virtio_init(void)
{

    s_gnss_vdev = kmalloc(sizeof(*s_gnss_vdev), GFP_KERNEL);

    register_virtio_driver(&virtio_char_gnss_driver);

    register_gnss_device();
	return 0;
}

static void __exit gnss_virtio_exit(void)
{
    kfree(s_gnss_vdev);
}

module_init(gnss_virtio_init);
module_exit(gnss_virtio_exit);

MODULE_AUTHOR("Bo Hu <bohu@google.com>");
MODULE_DESCRIPTION("GNSS virtio serial driver");
MODULE_SOFTDEP("pre: gnss_serial");
MODULE_SOFTDEP("pre: virtio_pci");
MODULE_LICENSE("GPL v2");
