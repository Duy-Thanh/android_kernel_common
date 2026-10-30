// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Google LLC */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef __EXPORTED_HEADERS__
#define __EXPORTED_HEADERS__
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <linux/android_fuse.h>
#include <linux/bpf.h>
#include <linux/fuse.h>

#include "../../kselftest_harness.h"

#define FUSE_BPF_FEAT_PATH	"/sys/fs/fuse/features/fuse_bpf"
#define FUSE_BPF_TYPE_PATH	"/sys/fs/fuse/bpf_prog_type_fuse"

#define FUSE_BPF_MOV64_IMM(DST, IMM) \
	((struct bpf_insn){ .code = BPF_ALU64 | BPF_MOV | BPF_K, \
			    .dst_reg = (DST), .imm = (IMM) })
#define FUSE_BPF_LDX_MEM(SZ, DST, SRC, OFF) \
	((struct bpf_insn){ .code = BPF_LDX | (SZ) | BPF_MEM, \
			    .dst_reg = (DST), .src_reg = (SRC), .off = (OFF) })
#define FUSE_BPF_STX_MEM(SZ, DST, SRC, OFF) \
	((struct bpf_insn){ .code = BPF_STX | (SZ) | BPF_MEM, \
			    .dst_reg = (DST), .src_reg = (SRC), .off = (OFF) })
#define FUSE_BPF_JMP_IMM(OP, DST, IMM, OFF) \
	((struct bpf_insn){ .code = BPF_JMP | (OP) | BPF_K, \
			    .dst_reg = (DST), .off = (OFF), .imm = (IMM) })
#define FUSE_BPF_EXIT_INSN() \
	((struct bpf_insn){ .code = BPF_JMP | BPF_EXIT })

static inline uint64_t ptr_to_u64(const void *ptr)
{
	return (uint64_t)(uintptr_t)ptr;
}

static void close_fd(int *fd)
{
	if (*fd >= 0) {
		close(*fd);
		*fd = -1;
	}
}

static int read_sysfs_str(const char *path, char *buf, size_t len)
{
	int err = 0, fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t n;

	if (fd < 0)
		return -errno;
	n = read(fd, buf, len - 1);
	if (n < 0)
		err = -errno;
	else
		buf[n] = '\0';
	close(fd);
	return err;
}

static void dump_bpf_prog_log(union bpf_attr *attr, int saved_errno)
{
	char log_buf[4096] = { 0 };
	const char *nl, *p;
	int retry_fd;

	attr->log_level = 1;
	attr->log_size = sizeof(log_buf);
	attr->log_buf = ptr_to_u64(log_buf);
	retry_fd = (int)syscall(__NR_bpf, BPF_PROG_LOAD, attr, sizeof(*attr));
	if (retry_fd >= 0)
		close(retry_fd);
	fprintf(stderr, "# BPF_PROG_LOAD failed: %s (errno=%d)\n",
		strerror(saved_errno), saved_errno);
	if (log_buf[0] == '\0')
		return;
	fprintf(stderr, "# Verifier log:\n");
	for (p = log_buf; *p != '\0'; p = nl ? nl + 1 : p + strlen(p)) {
		nl = strchr(p, '\n');
		fprintf(stderr, "#   %.*s\n",
			(int)(nl ? (size_t)(nl - p) : strlen(p)), p);
	}
}

static int load_fuse_bpf_prog_impl(const struct bpf_insn *insns,
				   size_t insn_cnt, bool dump_log)
{
	static const char gpl_license[] = "GPL";
	uint32_t prog_type = (uint32_t)BPF_PROG_TYPE_FUSE;
	union bpf_attr attr = { 0 };
	char buf[32], *end = NULL;
	int fd, saved_errno;

	if (read_sysfs_str(FUSE_BPF_TYPE_PATH, buf, sizeof(buf)) == 0) {
		unsigned long val = strtoul(buf, &end, 10);

		if (end != buf && val > 0 && val <= UINT32_MAX)
			prog_type = (uint32_t)val;
	}
	attr.prog_type = prog_type;
	attr.insn_cnt = (uint32_t)insn_cnt;
	attr.insns = ptr_to_u64(insns);
	attr.license = ptr_to_u64(gpl_license);
	fd = (int)syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
	if (fd < 0 && dump_log && errno != EPERM) {
		saved_errno = errno;
		dump_bpf_prog_log(&attr, saved_errno);
		errno = saved_errno;
	}
	return fd;
}

static int load_fuse_bpf_prog(const struct bpf_insn *insns, size_t insn_cnt)
{
	return load_fuse_bpf_prog_impl(insns, insn_cnt, true);
}

static int load_fuse_bpf_prog_quiet(const struct bpf_insn *insns,
				     size_t insn_cnt)
{
	return load_fuse_bpf_prog_impl(insns, insn_cnt, false);
}

FIXTURE(fuse_bpf)
{
	int bpf_fd;
};

FIXTURE_SETUP(fuse_bpf)
{
	char feat[32] = { 0 };
	int ret;

	self->bpf_fd = -1;
	ret = read_sysfs_str(FUSE_BPF_FEAT_PATH, feat, sizeof(feat));
	if (ret == -ENOENT || ret == -EACCES)
		SKIP(return, "fuse-bpf not supported");
	ASSERT_EQ(0, ret);
	if (strcmp(feat, "supported\n") != 0)
		SKIP(return, "fuse-bpf not supported");
}

FIXTURE_TEARDOWN(fuse_bpf)
{
	close_fd(&self->bpf_fd);
}

TEST_F(fuse_bpf, sysfs_and_verifier_gate)
{
	const struct bpf_insn valid_prog[] = {
		FUSE_BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				 offsetof(struct fuse_bpf_args, opcode)),
		FUSE_BPF_MOV64_IMM(BPF_REG_0, FUSE_BPF_BACKING),
		FUSE_BPF_EXIT_INSN(),
	};
	const struct bpf_insn oob_read_prog[] = {
		FUSE_BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				 (int16_t)(sizeof(struct fuse_bpf_args) + 8)),
		FUSE_BPF_MOV64_IMM(BPF_REG_0, FUSE_BPF_BACKING),
		FUSE_BPF_EXIT_INSN(),
	};
	const struct bpf_insn ro_write_prog[] = {
		FUSE_BPF_MOV64_IMM(BPF_REG_2, 0),
		FUSE_BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_2,
				 offsetof(struct fuse_bpf_args, opcode)),
		FUSE_BPF_MOV64_IMM(BPF_REG_0, FUSE_BPF_BACKING),
		FUSE_BPF_EXIT_INSN(),
	};
	char feat[32] = { 0 };
	int bad_fd;

	ASSERT_EQ(0, read_sysfs_str(FUSE_BPF_FEAT_PATH, feat, sizeof(feat)));
	ASSERT_STREQ("supported\n", feat);

	self->bpf_fd = load_fuse_bpf_prog(valid_prog, ARRAY_SIZE(valid_prog));
	if (self->bpf_fd < 0 && (errno == EPERM || errno == EACCES))
		SKIP(return, "CAP_BPF required");
	ASSERT_GE(self->bpf_fd, 0);

	errno = 0;
	bad_fd = load_fuse_bpf_prog_quiet(oob_read_prog,
					  ARRAY_SIZE(oob_read_prog));
	if (bad_fd >= 0)
		close(bad_fd);
	ASSERT_LT(bad_fd, 0);
	EXPECT_TRUE(errno == EACCES || errno == EINVAL);

	errno = 0;
	bad_fd = load_fuse_bpf_prog_quiet(ro_write_prog,
					  ARRAY_SIZE(ro_write_prog));
	if (bad_fd >= 0)
		close(bad_fd);
	ASSERT_LT(bad_fd, 0);
	EXPECT_TRUE(errno == EACCES || errno == EINVAL);
}

TEST_HARNESS_MAIN
