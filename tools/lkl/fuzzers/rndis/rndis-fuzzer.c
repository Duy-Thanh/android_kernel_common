/* SPDX-License-Identifier: GPL-2.0 */
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

#include <lkl/linux/ioctl.h>
#include <lkl/linux/usbdevice_fs.h>

#define LOG(fmt, ...)                                                          \
	if (g_log_enabled) {                                                   \
		printf(fmt, ##__VA_ARGS__);                                    \
	}

static bool g_log_enabled = true;

// We assume that f_rndis is device #2 on the loopback USB bus #1 which is the
// case for the current configuration.
static const char *g_rndis_usbdevfs = "/dev/bus/usb/001/002";

#define __le32 unsigned int

typedef struct rndis_set_msg_type {
	__le32 MessageType;
	__le32 MessageLength;
	__le32 RequestID;
	__le32 OID;
	__le32 InformationBufferLength;
	__le32 InformationBufferOffset;
	__le32 DeviceVcHandle;
} rndis_set_msg_type;

#define USB_DIR_OUT 0
#define USB_TYPE_CLASS (0x01 << 5)
#define USB_RECIP_INTERFACE 0x01

static int usbfs_send_control_request(const char *target_device,
				      unsigned char request_type,
				      unsigned char request,
				      unsigned short value,
				      unsigned short index,
				      unsigned short length, void *data)
{
	int fd = lkl_sys_open(target_device, O_RDWR, S_IRUSR | S_IWUSR);
	if (fd == -1)
		return -1;

	struct lkl_usbdevfs_ctrltransfer ctrl_req;
	ctrl_req.bRequestType = request_type;
	ctrl_req.bRequest = request;
	ctrl_req.wValue = value;
	ctrl_req.wIndex = index;
	ctrl_req.wLength = length;
	ctrl_req.timeout = 0;
	ctrl_req.data = data;

	if (lkl_sys_ioctl(fd, LKL_USBDEVFS_CONTROL, (unsigned long)&ctrl_req) < 0) {
		lkl_sys_close(fd);
		return -1;
	}

	if (lkl_sys_close(fd) < 0)
		return -1;

	return 0;
}

// Implementation of lkl_mount_fs cannot be used to mount devtmpfs system, thus,
// implement it below (see b/172868430).
static int mount_devfs()
{
	int ret = lkl_sys_mkdir("/dev", 0770);
	if (ret && ret != -LKL_EEXIST) {
		return -1;
	}

	ret = lkl_sys_mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
	if (ret && ret != -LKL_EBUSY) {
		return -1;
	}

	return 0;
}

static int read_data_from_file(const char *file, void *value, int size)
{
	int fd = lkl_sys_open(file, O_RDONLY, S_IRUSR | S_IWUSR);
	if (fd == -1)
		return -1;

	int bytes_read = lkl_sys_read(fd, value, size);
	if (bytes_read != size) {
		lkl_sys_close(fd);
		return -1;
	}

	if (lkl_sys_close(fd) < 0)
		return -1;

	return 0;
}

static int dummy_udc_vbus_draw(int enable)
{
	int fd;
	const char *value;

	if (enable)
		value = "dummy_udc.0";
	else
		value = "none";

	fd = lkl_sys_open("/configfs/usb_gadget/g1/UDC", O_WRONLY,
			  S_IRUSR | S_IWUSR);
	if (fd < 0)
		return -1;

	if (strlen(value) != lkl_sys_write(fd, value, strlen(value))) {
		lkl_sys_close(fd);
		return -1;
	}

	lkl_sys_close(fd);
	return 0;
}

static int init_usb_gadget_rndis()
{
	int err = 0;

	err = lkl_sys_mkdir("/configfs/usb_gadget/g1", 0770);
	if (err != 0)
		return err;

	err = lkl_sys_mkdir("/configfs/usb_gadget/g1/functions/rndis.gs1",
			    0770);
	if (err != 0)
		return err;

	err = lkl_sys_mkdir("/configfs/usb_gadget/g1/configs/b.1", 0770);
	if (err != 0)
		return err;

	err = lkl_sys_symlink("/configfs/usb_gadget/g1/functions/rndis.gs1",
			      "/configfs/usb_gadget/g1/configs/b.1/function0");
	if (err != 0)
		return err;

	// Don't check return value of the first call to dummy_udc_vbus_draw routine
	// as it can return a negative value if USB gadget subsystem hasn't been
	// initialized yet.
	dummy_udc_vbus_draw(0);
	err = dummy_udc_vbus_draw(1);
	if (err != 0)
		return err;

	// Wait until USB subsystem enumerates devices on the USB bus. This is done in
	// a kernel worker thread and can take some time.
	usleep(1000 * 1000);
	return 0;
}

static int lkl_init()
{
	if (!g_log_enabled) {
		lkl_host_ops.print = NULL;
	}

	int ret = lkl_start_kernel(&lkl_host_ops, "mem=50M loglevel=8");
	if (ret) {
		LOG("lkl_start_kernel failed\n");
		return -1;
	}

	lkl_mount_fs("sysfs");
	lkl_mount_fs("proc");
	// mount configfs in order to configure USB gadget subsystem
	lkl_mount_fs("configfs");

	ret = mount_devfs();
	if (ret != 0)
		return -1;

	ret = init_usb_gadget_rndis();
	if (ret != 0)
		return -1;

	return 0;
}

void __llvm_profile_initialize_file(void);
int __llvm_profile_write_file(void);

void flush_coverage()
{
	LOG("Flushing coverage data...\n");
	__llvm_profile_write_file();
	LOG("Done...\n");
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	for (int i = 0; i < *argc; i++) {
		if (strcmp((*argv)[i], "-quiet=1") == 0) {
			g_log_enabled = false;
			break;
		}
	}

	lkl_init();

	__llvm_profile_initialize_file();
	atexit(flush_coverage);
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	static int iter = 0;
	// Copy the mutated input to a temp buffer allocated on the stack to avoid
	// LKL KASan failure due to unmapped KASan shadow memory - shadow memory for
	// the thread stack is always mapped (an alternative solution would be to
	// reallocate the buffer using lkl_malloc routine).
	char rndis_packet[512];
	size_t packet_size =
		Size > sizeof(rndis_packet) ? sizeof(rndis_packet) : Size;
	memcpy(rndis_packet, Data, packet_size);

	usbfs_send_control_request(g_rndis_usbdevfs,
				   USB_DIR_OUT | USB_TYPE_CLASS |
					   USB_RECIP_INTERFACE,
				   0x00, 0x00, 0x00, packet_size, rndis_packet);
	iter++;
	if (iter > 1000) {
		flush_coverage();
		iter = 0;
	}
	return 0;
}
