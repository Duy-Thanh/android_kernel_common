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

#define LOG(fmt, ...)                                                          \
	if (g_log_enabled) {                                                   \
		printf(fmt, ##__VA_ARGS__);                                    \
	}

bool g_log_enabled = true;

struct lkl_disk_context {
	char *data;
	size_t size;
};

static int disk_get_capacity(struct lkl_disk disk, unsigned long long *res)
{
	struct lkl_disk_context *disk_context =
		(struct lkl_disk_context *)disk.handle;
	*res = disk_context->size;
	return 0;
}

static int do_rw(int read_write, struct lkl_disk_context *disk_context,
		 struct lkl_blk_req *req)
{
	off_t off = req->sector * 512;
	void *addr;
	size_t len;
	int i;
	int ret = 0;

	for (i = 0; i < req->count; i++) {
		addr = req->buf[i].iov_base;
		len = req->buf[i].iov_len;

		if ((size_t)off >= disk_context->size || off < 0) {
			return -1;
		} else {
			ret = len > (disk_context->size - off) ?
					    disk_context->size - off :
					    len;
			if (read_write) { // read operation
				memcpy(addr, disk_context->data + off, ret);
			} else { // write operation
				memcpy(disk_context->data + off, addr, ret);
			}
		}
	}

	return ret;
}

static int disk_blk_request(struct lkl_disk disk, struct lkl_blk_req *req)
{
	int err = 0;
	struct lkl_disk_context *disk_context =
		(struct lkl_disk_context *)disk.handle;

	switch (req->type) {
	case LKL_DEV_BLK_TYPE_READ:
		err = do_rw(1, disk_context, req);
		break;
	case LKL_DEV_BLK_TYPE_WRITE:
		err = do_rw(0, disk_context, req);
		break;
	case LKL_DEV_BLK_TYPE_FLUSH:
	case LKL_DEV_BLK_TYPE_FLUSH_OUT:
		break;
	default:
		return LKL_DEV_BLK_STATUS_UNSUP;
	}

	if (err < 0)
		return LKL_DEV_BLK_STATUS_IOERR;

	return LKL_DEV_BLK_STATUS_OK;
}

struct lkl_dev_blk_ops lkl_vfat_fuzzer_ops = {
	.get_capacity = disk_get_capacity,
	.request = disk_blk_request,
};

static int disk_add(char *disk_image, size_t disk_size,
		    struct lkl_disk_context *disk_ctx, struct lkl_disk *disk)
{
	disk_ctx->data = disk_image;
	disk_ctx->size = disk_size;

	disk->handle = (void *)disk_ctx;
	disk->ops = &lkl_vfat_fuzzer_ops;

	return lkl_disk_add(disk);
}

static int disk_remove(struct lkl_disk *disk)
{
	int ret;
	ret = lkl_disk_remove(*disk);
	if (ret == 0)
		return 0;
	return -1;
}

static int umount_dev(int disk_id)
{
	long ret, ret2;

	ret = lkl_sys_chdir("/");

	ret2 = lkl_umount_dev(disk_id, 0, 0, 1000);

	if (!ret && !ret2)
		return 0;

	return -1;
}

static int read_dir(struct lkl_dir *dir)
{
	struct lkl_linux_dirent64 *de = lkl_readdir(dir);
	while (de) {
		de = lkl_readdir(dir);
	}
	if (lkl_errdir(dir) == 0)
		return 0;
	return -1;
}

static void list_rootdir(const char *mount_point)
{
	struct lkl_dir *dir = NULL;
	int err = 0;

	if (lkl_sys_chdir(mount_point) != 0)
		return;

	dir = lkl_opendir(mount_point, &err);
	if (dir == NULL)
		return;

	read_dir(dir);
	lkl_closedir(dir);
}

static int create_file(const char *file_path)
{
	char tmp[16];
	int fd;

	memset(tmp, 0x01, sizeof(tmp));
	fd = lkl_sys_open(file_path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0)
		return -1;

	if (sizeof(tmp) != lkl_sys_write(fd, tmp, sizeof(tmp))) {
		lkl_sys_close(fd);
		return -1;
	}

	lkl_sys_close(fd);
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
	lkl_mount_fs("dev");

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
	char mnt_point[32];
	char *disk_image = NULL;
	struct lkl_disk_context lkl_disk_ctx;
	struct lkl_disk lkl_disk;
	int lkl_disk_id = 0;
	char path[256];
	char new_path[256];

	disk_image = malloc(Size);
	if (disk_image == NULL) {
		LOG("Failed to allocate memory for disk image of size %zx\n",
		    Size);
		return 0;
	}

	memcpy(disk_image, Data, Size);

	LOG("Begin xxx\n");

	lkl_disk_id = disk_add(disk_image, Size, &lkl_disk_ctx, &lkl_disk);
	if (lkl_disk_id < 0) {
		LOG("Failed to add disk.\n");
		free(disk_image);
		return 0;
	}
	LOG("disk_add disk_id %d\n", lkl_disk_id);

	if (lkl_mount_dev(lkl_disk_id, 0, "vfat", 0, NULL, mnt_point,
			  sizeof(mnt_point)) == 0) {
		list_rootdir(mnt_point);

		// TODO(b/170988855): replace hardcoded dir and file names with
                // libFuzzer-provided input.
		snprintf(path, sizeof(path), "%s/%s", mnt_point, "tmp_dir");
		if (lkl_sys_mkdir(path, 0777) != 0) {
			LOG("Failed to create directory %s\n", path);
		}

		snprintf(path, sizeof(path), "%s/%s/%s", mnt_point, "tmp_dir",
			 "tmp_file");
		if (create_file(path) != 0) {
			LOG("Failed to create file %s\n", path);
		}

		snprintf(new_path, sizeof(new_path), "%s/%s", mnt_point,
			 "new_tmp_file");
		if (lkl_sys_rename(path, new_path) != 0) {
			LOG("Failed to rename file %s to %s\n", path, new_path);
		}

		if (lkl_sys_unlink(new_path) != 0) {
			LOG("Failed to unlink file %s\n", new_path);
		}

		snprintf(path, sizeof(path), "%s/%s", mnt_point, "tmp_dir");
		if (lkl_sys_rmdir(path) != 0) {
			LOG("Failed to remove directory %s\n", path);
		}

		if (umount_dev(lkl_disk_id) != 0) {
			LOG("Failed to unmount disk.\n");
		}
	}

	disk_remove(&lkl_disk);

	free(disk_image);

	iter++;
	if (iter > 1000) {
		flush_coverage();
		iter = 0;
	}

	LOG("Done.\n");

	return 0;
}
