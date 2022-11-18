/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/syscalls.h>
#include <linux/platform_device.h>
#include <linux/syscalls.h>
#include <linux/types.h>


SYSCALL_DEFINE3(virtio_mmio_device_add, long, base, long, size, unsigned int,
		irq)
{
	struct platform_device *pdev;
	int ret;

	struct resource res[] = {
		[0] = {
		       .start = base,
		       .end = base + size - 1,
		       .flags = IORESOURCE_MEM,
		       },
		[1] = {
		       .start = irq,
		       .end = irq,
		       .flags = IORESOURCE_IRQ,
		       },
	};

	pdev = platform_device_alloc("virtio-mmio", PLATFORM_DEVID_AUTO);
	if (!pdev) {
		dev_err(&pdev->dev,
			"%s: Unable to device alloc for virtio-mmio\n",
			__func__);
		return -ENOMEM;
	}

	ret = platform_device_add_resources(pdev, res, ARRAY_SIZE(res));
	if (ret) {
		dev_err(&pdev->dev, "%s: Unable to add resources for %s%d\n",
			__func__, pdev->name, pdev->id);
		goto exit_device_put;
	}

	ret = platform_device_add(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s: Unable to add %s%d\n", __func__,
			pdev->name, pdev->id);
		goto exit_release_pdev;
	}

	return pdev->id;

exit_release_pdev:
	platform_device_del(pdev);
exit_device_put:
	platform_device_put(pdev);

	return ret;
}

static int remove_platform_device_by_name(const char __user *device_name,
					  size_t device_name_length)
{
	struct platform_device *pdev;
	struct device *dev;

	if (!access_ok(device_name, device_name_length))
		return -EFAULT;

	dev = bus_find_device_by_name(&platform_bus_type, NULL, device_name);
	if (dev == NULL)
		return -ENODEV;

	pdev = container_of(dev, struct platform_device, dev);
	platform_device_unregister(pdev);

	put_device(dev);

	return 0;
}

SYSCALL_DEFINE2(virtio_mmio_device_remove, const char __user *, device_name,
		size_t, count)
{
	return remove_platform_device_by_name(device_name, count);
}

SYSCALL_DEFINE2(lkl_pci_bus_add, const char __user *, base_name, size_t, count)
{
	int ret;
	struct platform_device *dev;

	if (!access_ok(base_name, count))
		return -EFAULT;

	dev = platform_device_alloc(base_name, -1);
	if (!dev)
		return -ENOMEM;

	ret = platform_device_add(dev);
	if (ret != 0)
		goto error;

	return 0;
error:
	platform_device_put(dev);
	return ret;
}

SYSCALL_DEFINE2(lkl_pci_bus_remove, const char __user *, device_name, size_t,
		count)
{
	return remove_platform_device_by_name(device_name, count);
}