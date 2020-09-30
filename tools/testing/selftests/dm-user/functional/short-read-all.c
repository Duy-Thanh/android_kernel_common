// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2020 Google, Inc
 * Originally selftests/futex/functional/futex_wait_timeout.c
 * Copyright © International Business Machines  Corp., 2009
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <pthread.h>
#include <linux/dm-user.h>
#include <sys/prctl.h>
#include "logging.h"

#define SECTOR_SIZE 512

#define MAX(a, b) ((a) > (b) ? (a) : (b))

struct test_context {
	char *block_dev;
	char *control_dev;
	size_t block_bytes;
	size_t read_bytes;
	char *store;
};

int write_all(int fd, void *buf, size_t len)
{
	char *buf_c = buf;
	ssize_t total = 0;
	ssize_t once;

	while (total < len) {
		size_t max = len - total;
		if (max > 3)
			max = max / 3;

		once = write(fd, buf_c + total, max);
		if (once <= 0) {
			perror("write_all failed");
			return -1;
		}
		total += once;
	}

	return 0;
}

int read_all(int fd, void *buf, size_t len)
{
	char *buf_c = buf;
	ssize_t total = 0;
	ssize_t once;

	while (total < len) {
		size_t max = len - total;
		if (max > 3)
			max = max / 3;

		once = read(fd, buf_c + total, max);
		if (once <= 0) {
			perror("read_all failed");
			return -1;
		}
		total += once;
	}

	return 0;
}

void *simple_daemon(void *context_uc)
{
	struct test_context *context = context_uc;
	char *store = context->store;

	int control_fd = open(context->control_dev, O_RDWR);
	if (control_fd < 0) {
		ksft_print_msg("Unable to open control device %s\n", context->control_dev);
		return (void *)(RET_FAIL);
	}

	while (1) {
		struct dm_user_message msg;
		char *base;

		if (read_all(control_fd, &msg, sizeof(msg))) {
			perror("unable to read msg");
			return (void *)(RET_FAIL);
		}

		base = store + msg.sector * SECTOR_SIZE;
		if (base + msg.len > store + context->block_bytes) {
			ksft_print_msg("access out of bounds\n");
			return (void *)(RET_FAIL);
		}

		if (msg.type == DM_USER_MAP_WRITE) {
			if (read_all(control_fd, base, msg.len)) {
				perror("unable to read buf");
				return (void *)(RET_FAIL);
			}
		}

		if (write_all(control_fd, &msg, sizeof(msg))) {
			perror("unable to write msg");
			return (void *)(RET_FAIL);
		}

		if (msg.type == DM_USER_MAP_READ) {
			if (write_all(control_fd, base, msg.len)) {
				perror("unable to write buf");
				return (void *)(RET_FAIL);
			}
		}
	}

	/* The daemon doesn't actully terminate for this test. */
	perror("Unable to read from control device");
	return (void *)(RET_FAIL);
}

void *read_entire_device(void *context_uc)
{
	struct test_context *context = context_uc;
	void *out = NULL;

	int block_fd = open(context->block_dev, O_RDONLY);
	if (block_fd < 0) {
		ksft_print_msg("Unable to open block device\n");
		return (void *)(RET_FAIL);
	}

	char *buf = malloc(context->read_bytes);
	size_t off = 0;
	ssize_t readed;
	while ((readed = read(block_fd, buf, context->read_bytes)) > 0) {
		for (size_t i = 0; i < readed; ++i) {
			if (context->store[off + i] != buf[i]) {
				ksft_print_msg(
					"read data mismatch: byte %zu, expected %x got %x\n",
					off + i,
					(int)(__u8)(context->store[off + i]),
					(int)(__u8)(buf[i])
				);
				out = (void *)(RET_FAIL);
			}
		}
		off += readed;
	}

	free(buf);
	close(block_fd);

	if (off < context->block_bytes) {
		ksft_print_msg("Short block device\n");
		return (void *)(RET_FAIL);
	}

	return out;
}

void usage(char *prog)
{
	printf("Usage: %s\n", prog);
	printf("  -h			Display this help message\n");
	printf("  -v L			Verbosity level: %d=QUIET %d=CRITICAL %d=INFO\n",
	       VQUIET, VCRITICAL, VINFO);
	printf("  -b <block dev>	Block device to use for the test\n");
	printf("  -c <control dev>	Control device to use for the test\n");
	printf("  -s <sectors>		The number of sectors in the device\n");
	printf("  -r <read bytes>	The number of bytes at a time to read\n");
}

int main(int argc, char *argv[])
{
	int ret = RET_PASS;
	int c;
	struct test_context context = {
		.block_dev	= NULL,
		.control_dev	= NULL,
		.block_bytes	= 0,
		.read_bytes	= 4096,
	};
	pthread_t daemon, reader;
	void *pthread_ret;

	prctl(PR_SET_IO_FLUSHER, 0, 0, 0, 0);

	while ((c = getopt(argc, argv, "h:v:b:c:s:r:")) != -1) {
		switch (c) {
		case 'h':
			usage(basename(argv[0]));
			exit(0);
		case 'v':
			log_verbosity(atoi(optarg));
			break;
		case 'b':
			context.block_dev = strdup(optarg);
			break;
		case 'c':
			context.control_dev = strdup(optarg);
			break;
		case 's':
			context.block_bytes = atoi(optarg) * SECTOR_SIZE;
			break;
		case 'r':
			context.read_bytes = atoi(optarg);
			break;
		default:
			usage(basename(argv[0]));
			exit(1);
		}
	}

	ksft_print_header();
	ksft_set_plan(1);
	ksft_print_msg("%s: read_bytes=%zu\n",
		       basename(argv[0]),
		       context.read_bytes);

	ret = RET_PASS;

	context.store = malloc(context.block_bytes);
	for (size_t i = 0; i < context.block_bytes/sizeof(size_t); ++i)
		((size_t *)(context.store))[i] = i;

	if (pthread_create(&daemon, NULL, &simple_daemon, &context) < 0)
		ret = RET_ERROR;
	if (pthread_create(&reader, NULL, &read_entire_device, &context) < 0)
		ret = RET_ERROR;

	int done = 0;
	while (!done) {
		if (pthread_tryjoin_np(reader, &pthread_ret) == 0) {
			if (pthread_ret != NULL)
				ret = RET_FAIL;
			done = 1;
		}

		if (pthread_tryjoin_np(daemon, &pthread_ret) == 0) {
			ret = RET_ERROR;
			done = 1;
		}

		sleep(1);
	}

	print_result(basename(argv[0]), ret);
	exit(ret);
}
