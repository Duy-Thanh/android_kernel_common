// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC
 */

#include "test_fuse.h"

/* TODO #include <include/uapi/linux/bpf_fuse.h> */
#define FUSE_POSTFILTER		0x20000

static int display_trace()
{
	int result = TEST_FAILURE;
	int pid = -1;
	int tp = -1;
	char trace_buffer;
	ssize_t bytes_read;

	TEST(pid = fork(), pid != -1);
	if (pid != 0)
		return TEST_SUCCESS;

	TEST(tp = open("/sys/kernel/debug/tracing/trace_pipe",
		       O_RDONLY | O_CLOEXEC), tp != -1);
	for(;;) {
		TEST(bytes_read = read(tp, &trace_buffer, sizeof(trace_buffer)),
		     bytes_read == 1);
		printf("%c", trace_buffer);
	}
out:
	if (pid == 0) {
		close(tp);
		exit(TEST_FAILURE);
	}
	return result;
}

int main(int argc, char *argv[])
{
	int result = TEST_FAILURE;
	char *mount_dir = NULL;
	char *src_dir = NULL;
	int bpf_fd = -1;
	int src_fd = -1;
	int fuse_dev = -1;

	if (geteuid() != 0)
		ksft_print_msg("Not a root, might fail to mount.\n");

	display_trace();

	TEST(src_dir = setup_mount_dir("fd-src"), src_dir);
	TEST(mount_dir = setup_mount_dir("fd-dst"), mount_dir);
	TESTEQUAL(install_bpf("test_daemon.raw", &bpf_fd), 0);
	TEST(src_fd = open("fd-src", O_DIRECTORY | O_RDONLY | O_CLOEXEC),
	     src_fd != -1);
	TESTEQUAL(mount_fuse(mount_dir, bpf_fd, src_fd, &fuse_dev), 0);

	for(;;) {
		uint8_t bytes_in[FUSE_MIN_READ_BUFFER];
		uint8_t bytes_out[FUSE_MIN_READ_BUFFER];
		DECL_FUSE(open);
		struct fuse_in_header *in_header =
			(struct fuse_in_header *)bytes_in;
		ssize_t res = read(fuse_dev, bytes_in, sizeof(bytes_in));

		if (res == -1)
			break;

		switch(in_header->opcode) {
		case FUSE_OPEN:
			*open_out = (struct fuse_open_out) {
				.fh = 1,
				.open_flags = open_in->flags,
			};
			TESTFUSEOUT(open_out);
			break;
		case FUSE_READ: {
			const char *fake_data = "fake data";
			TESTFUSEOUTREAD(fake_data, strlen(fake_data));
			break;
		}

		case FUSE_FORGET:
		case FUSE_RELEASE:
		case FUSE_RELEASEDIR:
			break;

		case FUSE_FLUSH:
			TESTFUSEOUTEMPTY();
			break;

		case FUSE_READDIR | FUSE_POSTFILTER: {
			struct fuse_dirent *fuse_dirent =
				(struct fuse_dirent *) (bytes_in + res);

			*fuse_dirent = (struct fuse_dirent) {
				.ino = 100,
				.off = 5,
				.namelen = strlen("fake"),
				.type = DT_REG,
			};
			strcpy((char*)(bytes_in + res + sizeof(*fuse_dirent)), "fake");
			res += FUSE_DIRENT_ALIGN(sizeof(*fuse_dirent) + strlen("fake") + 1);
			TESTFUSEOUTREAD(bytes_in + sizeof(struct fuse_in_header),
					res - sizeof(struct fuse_in_header));
			break;
		}

		case FUSE_LOOKUP: {
			DECL_FUSE_OUT(entry);

			*entry_out = (struct fuse_entry_out) {
				.nodeid		= 2,
				.generation	= 1,
				.attr = (struct fuse_attr) {
					.ino = 100,
					.size = 4,
					.blksize = 512,
					.mode = S_IFREG,
				},
			};
			TESTFUSEOUT(entry_out);
			break;
		}

		case FUSE_GETATTR: {
			DECL_FUSE_OUT(attr);

			*attr_out = (struct fuse_attr_out) {
				.attr_valid = 1,
				.attr = (struct fuse_attr) {
					.ino = 100,
					.size = 4,
					.blksize = 512,
					.mode = S_IFREG,
				},
			};
			TESTFUSEOUT(attr_out);
			break;
		}

		default:
			printf("opcode is %d\n", in_header->opcode);
			break;
		}
	}

	result = TEST_SUCCESS;

out:
	umount2(mount_dir, MNT_FORCE);
	delete_dir_tree(mount_dir);
	rmdir(mount_dir);
	free(mount_dir);
	delete_dir_tree(src_dir);
	rmdir(src_dir);
	free(src_dir);
	return result;
}
