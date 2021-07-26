// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC
 */

#ifndef TEST_FUSE__H
#define TEST_FUSE__H

#define _GNU_SOURCE

#include "test_framework.h"

#include <alloca.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <linux/random.h>
#include <linux/stat.h>
#include <linux/unistd.h>

#include <include/uapi/linux/fuse.h>
#include <include/uapi/linux/bpf.h>

#define PAGE_SIZE 4096
#define FUSE_POSTFILTER 0x20000

struct _test_options test_options;

static inline char *concat_file_name(const char *dir, const char *file)
{
	char full_name[FILENAME_MAX] = "";

	if (snprintf(full_name, ARRAY_SIZE(full_name), "%s/%s", dir, file) < 0)
		return NULL;
	return strdup(full_name);
}

static inline char *setup_mount_dir(const char *name)
{
	struct stat st;
	char *current_dir = getcwd(NULL, 0);
	char *mount_dir = concat_file_name(current_dir, name);

	free(current_dir);
	if (stat(mount_dir, &st) == 0) {
		if (S_ISDIR(st.st_mode))
			return mount_dir;

		ksft_print_msg("%s is a file, not a dir.\n", mount_dir);
		return NULL;
	}

	if (mkdir(mount_dir, 0777)) {
		ksft_print_msg("Can't create mount dir.");
		return NULL;
	}

	return mount_dir;
}

static int delete_dir_tree(const char *dir_path)
{
	DIR *dir = NULL;
	struct dirent *dp;
	int result = 0;

	dir = opendir(dir_path);
	if (!dir) {
		result = -errno;
		goto out;
	}

	while ((dp = readdir(dir))) {
		char *full_path;

		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
			continue;

		full_path = concat_file_name(dir_path, dp->d_name);
		if (dp->d_type == DT_DIR)
			result = delete_dir_tree(full_path);
		else
			result = unlink(full_path);
		free(full_path);
		if (result)
			goto out;
	}

out:
	if (dir)
		closedir(dir);
	if (!result)
		rmdir(dir_path);
	return result;
}

#define TESTFUSEIN(_opcode, in_struct)					\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		ssize_t res = read(fuse_dev, &bytes_in,			\
			sizeof(bytes_in));				\
									\
		TESTEQUAL(in_header->opcode, _opcode);			\
		TESTEQUAL(res, sizeof(*in_header) + sizeof(*in_struct));\
	} while(false)

/* Special case lookup since it is asymmetric */
#define TESTFUSELOOKUP(expected)					\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		char *name = (char *) (bytes_in + sizeof(*in_header));	\
									\
		TESTEQUAL(read(fuse_dev, &bytes_in, sizeof(bytes_in)),	\
			  sizeof(*in_header) + strlen(expected) + 1);	\
		TESTEQUAL(in_header->opcode, FUSE_LOOKUP);		\
		TESTCOND(!strcmp(name, expected));			\
	} while(false)

#define TESTFUSEOUT(out_struct)						\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		struct fuse_out_header *out_header =			\
			(struct fuse_out_header *)bytes_out;		\
									\
		*out_header = (struct fuse_out_header) {		\
			.len = sizeof(*out_header) +			\
				sizeof(*out_struct),			\
			.unique = in_header->unique,			\
		};							\
		TESTEQUAL(write(fuse_dev, bytes_out, out_header->len),	\
			  out_header->len);				\
	} while(false)

#define TESTFUSEOUTEMPTY()						\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		struct fuse_out_header *out_header =			\
			(struct fuse_out_header *)bytes_out;		\
									\
		*out_header = (struct fuse_out_header) {		\
			.len = sizeof(*out_header),			\
			.unique = in_header->unique,			\
		};							\
		TESTEQUAL(write(fuse_dev, bytes_out, out_header->len),	\
			  out_header->len);				\
	} while(false)

#define TESTFUSEOUTREAD(data, length)					\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		struct fuse_out_header *out_header =			\
			(struct fuse_out_header *)bytes_out;		\
									\
		*out_header = (struct fuse_out_header) {		\
			.len = sizeof(*out_header) + length,		\
			.unique = in_header->unique,			\
		};							\
		memcpy(bytes_out + sizeof(*out_header), data, length);	\
		TESTEQUAL(write(fuse_dev, bytes_out, out_header->len),	\
			  out_header->len);				\
	} while(false)

#define DECL_FUSE_IN(name)						\
	struct fuse_##name##_in *name##_in =				\
		(struct fuse_##name##_in *)				\
		(bytes_in + sizeof(struct fuse_in_header));

#define DECL_FUSE_OUT(name)						\
	struct fuse_##name##_out *name##_out =				\
		(struct fuse_##name##_out *)				\
		(bytes_out + sizeof(struct fuse_out_header))

#define DECL_FUSE(name)							\
	DECL_FUSE_IN(name);						\
	DECL_FUSE_OUT(name)

#define FUSE_ACTION	TEST(pid = fork(), pid != -1);			\
			if (pid) {
#define FUSE_DAEMON	} else {
#define FUSE_DONE		exit(TEST_SUCCESS);			\
			}						\
			TESTEQUAL(waitpid(pid, &status, 0), pid);	\
			TESTEQUAL(status, TEST_SUCCESS);

static inline int mount_fuse(const char *mount_dir, int bpf_fd, int dir_fd,
			     int *fuse_dev_ptr)
{
	int result = TEST_FAILURE;
	int fuse_dev = -1;
	char options[FILENAME_MAX];
	uint8_t bytes_in[FUSE_MIN_READ_BUFFER];
	uint8_t bytes_out[FUSE_MIN_READ_BUFFER];
	DECL_FUSE(init);

	TEST(fuse_dev = open("/dev/fuse", O_RDWR | O_CLOEXEC), fuse_dev != -1);
	snprintf(options, FILENAME_MAX, "fd=%d,user_id=0,group_id=0,rootmode=0040000",
		 fuse_dev);
	if (bpf_fd != -1)
		snprintf(options + strlen(options),
			 sizeof(options) - strlen(options),
			 ",root_bpf=%d", bpf_fd);
	if (dir_fd != -1)
		snprintf(options + strlen(options),
			 sizeof(options) - strlen(options),
			 ",root_dir=%d", dir_fd);
	TESTSYSCALL(mount("ABC", mount_dir, "fuse", 0, options));

	TESTFUSEIN(FUSE_INIT, init_in);
	TESTEQUAL(init_in->major, FUSE_KERNEL_VERSION);
	TESTEQUAL(init_in->minor, FUSE_KERNEL_MINOR_VERSION);
	*init_out = (struct fuse_init_out) {
		.major = FUSE_KERNEL_VERSION,
		.minor = FUSE_KERNEL_MINOR_VERSION,
		.max_readahead = 4096,
		.flags = 0,
		.max_background = 0,
		.congestion_threshold = 0,
		.max_write = 4096,
		.time_gran = 1000,
		.max_pages = 12,
		.map_alignment = 4096,
	};
	TESTFUSEOUT(init_out);

	*fuse_dev_ptr = fuse_dev;
	fuse_dev = -1;
	result = TEST_SUCCESS;
out:
	close(fuse_dev);
	return result;
}

static inline int install_bpf(const char *name, int *fd)
{
	int result = TEST_FAILURE;
	char path[PATH_MAX] = {};
	char *last_slash;
	struct stat st;
	uint64_t *filter = NULL;
	int filter_fd = -1;
	union bpf_attr bpf_attr;
	char log[65536];

	TESTNE(readlink("/proc/self/exe", path, PATH_MAX), -1);
	TEST(last_slash = strrchr(path, '/'), last_slash);
	strcpy(last_slash + 1, name);
	TESTSYSCALL(stat(path, &st));
	TEST(filter = malloc(st.st_size), filter);
	TEST(filter_fd = open(path, O_RDONLY | O_CLOEXEC), filter_fd != -1);
	TESTEQUAL(read(filter_fd, filter, st.st_size), st.st_size);
	if (filter[st.st_size / sizeof(filter[0]) - 1] == 0)
		st.st_size -= sizeof(filter[0]);
	bpf_attr = (union bpf_attr) {
		.prog_type = BPF_PROG_TYPE_FUSE,
		.insn_cnt = st.st_size / 8,
		.insns = ptr_to_u64(filter),
		.license = ptr_to_u64("GPL"),
		.log_buf = test_options.verbose ? ptr_to_u64(log) : 0,
		.log_size = test_options.verbose ? sizeof(log) : 0,
		.log_level = test_options.verbose ? 2 : 0,
	};
	*fd = syscall(__NR_bpf, BPF_PROG_LOAD, &bpf_attr, sizeof(bpf_attr));
	if (test_options.verbose)
		ksft_print_msg("%s\n", log);
	if (*fd == -1 && errno == ENOSPC)
		ksft_print_msg("bpf log size too small!\n");
	TESTNE(*fd, -1);

	result = TEST_SUCCESS;
out:
	close(filter_fd);
	free(filter);
	return result;
}
#endif
