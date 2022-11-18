/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <lkl.h>
#include <lkl_host.h>
#ifndef __MINGW32__
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#else
#include <windows.h>
#endif
#include <assert.h>

#include "test.h"
#include "cla.h"

#define ARGS_BUFF_SIZE 512
#define MAX_ARGS 7

/*
 * Global arrays to copy arguments in order to prevent KASan violations
 * on accessing not mapped shadow memory (there migh be no mapped shadow
 * memory for the range of virtual address that contain arguments passed
 * to the executable. Globals are guaranteed to have a corresponding
 * KASan shadow memory).
*/
static char copied_args_buff[ARGS_BUFF_SIZE];
static char* copied_args[MAX_ARGS];

static struct {
	int printk;
	const char *disk;
	const char *fstype;
	int partition;
} cla;

struct cl_arg args[] = {
	{"disk", 'd', "disk file to use", 1, CL_ARG_STR, &cla.disk},
	{"partition", 'P', "partition to mount", 1, CL_ARG_INT, &cla.partition},
	{"type", 't', "filesystem type", 1, CL_ARG_STR, &cla.fstype},
	{0},
};


static struct lkl_disk disk;
static int disk_id = -1;

int lkl_test_disk_add(void)
{
#ifdef __MINGW32__
	disk.handle = CreateFile(cla.disk, GENERIC_READ | GENERIC_WRITE,
			       0, NULL, OPEN_EXISTING, 0, NULL);
	if (!disk.handle)
#else
	disk.fd = open(cla.disk, O_RDWR);
	if (disk.fd < 0)
#endif
		goto out_unlink;

	disk.ops = NULL;

	disk_id = lkl_disk_add(&disk);
	if (disk_id < 0)
		goto out_close;

	goto out;

out_close:
#ifdef __MINGW32__
	CloseHandle(disk.handle);
#else
	close(disk.fd);
#endif

out_unlink:
#ifdef __MINGW32__
	DeleteFile(cla.disk);
#else
	unlink(cla.disk);
#endif

out:
	lkl_test_logf("disk fd/handle %x disk_id %d", disk.fd, disk_id);

	if (disk_id >= 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int lkl_test_disk_remove(void)
{
	int ret;

	ret = lkl_disk_remove(disk);

#ifdef __MINGW32__
	CloseHandle(disk.handle);
#else
	close(disk.fd);
#endif

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}


static char mnt_point[32];

LKL_TEST_CALL(mount_dev, lkl_mount_dev, 0, disk_id, cla.partition, cla.fstype,
	      0, NULL, mnt_point, sizeof(mnt_point))

static int lkl_test_umount_dev(void)
{
	long ret, ret2;

	ret = lkl_sys_chdir("/");

	ret2 = lkl_umount_dev(disk_id, cla.partition, 0, 1000);

	lkl_test_logf("%ld %ld", ret, ret2);

	if (!ret && !ret2)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

struct lkl_dir *dir;

static int lkl_test_opendir(void)
{
	int err;

	dir = lkl_opendir(mnt_point, &err);

	lkl_test_logf("lkl_opedir(%s) = %d %s\n", mnt_point, err,
		      lkl_strerror(err));

	if (err == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int lkl_test_readdir(void)
{
	struct lkl_linux_dirent64 *de = lkl_readdir(dir);
	int wr = 0;

	while (de) {
		wr += lkl_test_logf("%s ", de->d_name);
		if (wr >= 70) {
			lkl_test_logf("\n");
			wr = 0;
			break;
		}
		de = lkl_readdir(dir);
	}

	if (lkl_errdir(dir) == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

LKL_TEST_CALL(closedir, lkl_closedir, 0, dir);
LKL_TEST_CALL(chdir_mnt_point, lkl_sys_chdir, 0, mnt_point);
LKL_TEST_CALL(start_kernel, lkl_start_kernel, 0, &lkl_host_ops,
	     "mem=32M loglevel=8");
LKL_TEST_CALL(stop_kernel, lkl_sys_halt, 0);

struct lkl_test tests[] = {
        // with LKL_CONFIG_KASAN lkl_start_kernel should be executed first
        // before any other routine using shadow memory does
	LKL_TEST(start_kernel),
	LKL_TEST(disk_add),
	LKL_TEST(mount_dev),
	LKL_TEST(chdir_mnt_point),
	LKL_TEST(opendir),
	LKL_TEST(readdir),
	LKL_TEST(closedir),
	LKL_TEST(umount_dev),
	LKL_TEST(disk_remove),
	LKL_TEST(stop_kernel),

};

static void panic_hndlr(void)
{
	printf("Kernel panic while running the test case.\n");
	lkl_test_print_log();
	assert(0);
}

static int copy_args(int argc, const char **argv)
{
	if (argc > MAX_ARGS)
		return -1;

	size_t current_pos = 0;
	for (int i = 0 ; i < argc ; i ++) {
		size_t arg_len = strlen(argv[i]) + 1;
		if (ARGS_BUFF_SIZE - current_pos < arg_len)
			return -1;

		memcpy(&copied_args_buff[current_pos], argv[i], arg_len);
		copied_args[i] = &copied_args_buff[current_pos];
		current_pos += arg_len;
	}

	return 0;
}

int main(int argc, const char **argv)
{
	if (copy_args(argc, argv) < 0)
		return -1;

	if (parse_args(argc, copied_args, args) < 0)
		return -1;

	lkl_host_ops.print = lkl_test_log;
	lkl_host_ops.panic = panic_hndlr;

	return lkl_test_run(tests, sizeof(tests)/sizeof(struct lkl_test),
			    "disk %s", cla.fstype);
}
