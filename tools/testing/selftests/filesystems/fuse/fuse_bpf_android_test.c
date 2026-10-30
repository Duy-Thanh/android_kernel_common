// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Google LLC */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef __EXPORTED_HEADERS__
#define __EXPORTED_HEADERS__
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#include <linux/android_fuse.h>
#include <linux/bpf.h>
#include <linux/fuse.h>

#include "../../kselftest_harness.h"

#define FUSE_BPF_FEAT_PATH	"/sys/fs/fuse/features/fuse_bpf"
#define FUSE_BPF_TYPE_PATH	"/sys/fs/fuse/bpf_prog_type_fuse"
#define FUSE_BACKED_NODEID	2ULL
#define FUSE_UNBACKED_NODEID	3ULL

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

static int load_default_backing_prog(void)
{
	const struct bpf_insn prog[] = {
		FUSE_BPF_MOV64_IMM(BPF_REG_0, FUSE_BPF_BACKING),
		FUSE_BPF_EXIT_INSN(),
	};

	return load_fuse_bpf_prog(prog, ARRAY_SIZE(prog));
}

static void rm_rf(const char *path)
{
	DIR *dir = opendir(path);
	struct dirent *de;
	char child[PATH_MAX];
	struct stat st;

	if (!dir) {
		unlink(path);
		return;
	}
	while ((de = readdir(dir)) != NULL) {
		int n;

		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		n = snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
		if (n > 0 && (size_t)n < sizeof(child)) {
			if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode))
				rm_rf(child);
			else
				unlink(child);
		}
	}
	closedir(dir);
	rmdir(path);
}

static int fuse_reply(int dev_fd, uint64_t unique, int32_t err,
		      const void *arg1, size_t len1,
		      const void *arg2, size_t len2)
{
	struct fuse_out_header oh = {
		.len = (uint32_t)sizeof(oh), .error = err, .unique = unique,
	};
	struct iovec iov[3] = { { .iov_base = &oh, .iov_len = sizeof(oh) } };
	int cnt = 1;

	if (!err && arg1 && len1) {
		iov[cnt] = (struct iovec){ (void *)(uintptr_t)arg1, len1 };
		oh.len += (uint32_t)len1;
		cnt++;
	}
	if (!err && arg2 && len2) {
		iov[cnt] = (struct iovec){ (void *)(uintptr_t)arg2, len2 };
		oh.len += (uint32_t)len2;
		cnt++;
	}
	return writev(dev_fd, iov, cnt) < 0 ? -errno : 0;
}

FIXTURE(fuse_bpf)
{
	char tmp_dir[128], backing_dir[256], mnt_dir[256];
	int fuse_dev_fd, stop_pipe[2], bpf_fd;
	bool mounted, thread_started;
	pthread_t daemon_tid;
	uint32_t lookup_cnt, rw_cnt, unlink_cnt, canonical_cnt;
};

static void daemon_lookup(struct _test_data_fuse_bpf *self,
			  const struct fuse_in_header *ih, const char *name)
{
	struct fuse_entry_out eo = { .entry_valid = 3600, .attr_valid = 3600 };

	__atomic_add_fetch(&self->lookup_cnt, 1, __ATOMIC_SEQ_CST);
	if (ih->nodeid == FUSE_ROOT_ID && !strcmp(name, "backed")) {
		int prog_fd = __atomic_load_n(&self->bpf_fd, __ATOMIC_SEQ_CST);
		int dir_fd = open(self->backing_dir,
				  O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		struct fuse_entry_bpf_out bo = {
			.backing_action = FUSE_ACTION_REPLACE,
			.bpf_action = prog_fd >= 0 ?
				      FUSE_ACTION_REPLACE : FUSE_ACTION_KEEP,
			.bpf_fd = prog_fd >= 0 ?
				  (uint64_t)(uint32_t)prog_fd : 0,
		};

		if (dir_fd < 0) {
			fuse_reply(self->fuse_dev_fd, ih->unique, -errno,
				   NULL, 0, NULL, 0);
			return;
		}
		bo.backing_fd = (uint64_t)(uint32_t)dir_fd;
		eo.nodeid = FUSE_BACKED_NODEID;
		eo.attr.ino = FUSE_BACKED_NODEID;
		eo.attr.mode = S_IFDIR | 0755;
		eo.attr.nlink = 2;
		fuse_reply(self->fuse_dev_fd, ih->unique, 0,
			   &eo, sizeof(eo), &bo, sizeof(bo));
		close(dir_fd);
	} else if (ih->nodeid == FUSE_ROOT_ID && !strcmp(name, "unbacked")) {
		eo.nodeid = FUSE_UNBACKED_NODEID;
		eo.attr.ino = FUSE_UNBACKED_NODEID;
		eo.attr.mode = S_IFREG | 0644;
		eo.attr.nlink = 1;
		eo.attr.size = 9;
		fuse_reply(self->fuse_dev_fd, ih->unique, 0,
			   &eo, sizeof(eo), NULL, 0);
	} else {
		fuse_reply(self->fuse_dev_fd, ih->unique, -ENOENT,
			   NULL, 0, NULL, 0);
	}
}

static void *fuse_mini_daemon(void *arg)
{
	struct _test_data_fuse_bpf *self = arg;
	uint64_t req[(FUSE_MIN_READ_BUFFER + 4096) / sizeof(uint64_t)];
	struct pollfd pfds[2] = {
		{ .fd = self->fuse_dev_fd, .events = POLLIN },
		{ .fd = self->stop_pipe[0], .events = POLLIN },
	};

	for (;;) {
		const struct fuse_in_header *ih = (const void *)req;
		const char *body = (const char *)(ih + 1);
		uint32_t op;
		ssize_t n;

		if (poll(pfds, 2, -1) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if ((pfds[1].revents & (POLLIN | POLLHUP | POLLERR)) ||
		    (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)))
			break;
		if (!(pfds[0].revents & POLLIN))
			continue;

		n = read(self->fuse_dev_fd, req, sizeof(req));
		if (n < 0 && (errno == EINTR || errno == ENOENT))
			continue;
		if (n < (ssize_t)sizeof(*ih))
			break;

		op = ih->opcode & FUSE_OPCODE_FILTER;
		if (op == FUSE_FORGET || op == FUSE_BATCH_FORGET)
			continue;
		if (op == FUSE_INIT) {
			const struct fuse_init_in *in = (const void *)body;
			struct fuse_init_out out = {
				.major = FUSE_KERNEL_VERSION,
				.minor = in->minor < FUSE_KERNEL_MINOR_VERSION ?
					 in->minor : FUSE_KERNEL_MINOR_VERSION,
				.max_readahead = in->max_readahead,
				.flags = in->flags & FUSE_ATOMIC_O_TRUNC,
				.max_background = 16,
				.congestion_threshold = 12,
				.max_write = 4096,
				.time_gran = 1,
			};

			fuse_reply(self->fuse_dev_fd, ih->unique, 0,
				   &out, sizeof(out), NULL, 0);
		} else if (op == FUSE_GETATTR &&
			   (ih->nodeid == FUSE_ROOT_ID ||
			    ih->nodeid == FUSE_UNBACKED_NODEID)) {
			bool root = ih->nodeid == FUSE_ROOT_ID;
			struct fuse_attr_out ao = {
				.attr_valid = 3600, .attr.ino = ih->nodeid,
				.attr.mode = root ? (S_IFDIR | 0755) :
						    (S_IFREG | 0644),
				.attr.nlink = root ? 2 : 1,
				.attr.size = root ? 0 : 9,
			};

			fuse_reply(self->fuse_dev_fd, ih->unique, 0,
				   &ao, sizeof(ao), NULL, 0);
		} else if (op == FUSE_LOOKUP) {
			daemon_lookup(self, ih, body);
		} else if (op == FUSE_CANONICAL_PATH) {
			__atomic_add_fetch(&self->canonical_cnt, 1,
					   __ATOMIC_SEQ_CST);
			fuse_reply(self->fuse_dev_fd, ih->unique, 0,
				   self->backing_dir,
				   strlen(self->backing_dir) + 1, NULL, 0);
		} else if (op == FUSE_OPEN || op == FUSE_OPENDIR) {
			struct fuse_open_out oo = { .fh = 1 };

			fuse_reply(self->fuse_dev_fd, ih->unique, 0,
				   &oo, sizeof(oo), NULL, 0);
		} else if (op == FUSE_READ || op == FUSE_WRITE) {
			__atomic_add_fetch(&self->rw_cnt, 1, __ATOMIC_SEQ_CST);
			fuse_reply(self->fuse_dev_fd, ih->unique,
				   (op == FUSE_READ &&
				    ih->nodeid == FUSE_UNBACKED_NODEID) ?
				   0 : -EIO, "fallback\n", 9, NULL, 0);
		} else if (op == FUSE_UNLINK) {
			__atomic_add_fetch(&self->unlink_cnt, 1,
					   __ATOMIC_SEQ_CST);
			fuse_reply(self->fuse_dev_fd, ih->unique, -EOPNOTSUPP,
				   NULL, 0, NULL, 0);
		} else if (op == FUSE_FLUSH || op == FUSE_RELEASE ||
			   op == FUSE_RELEASEDIR || op == FUSE_DESTROY) {
			fuse_reply(self->fuse_dev_fd, ih->unique, 0,
				   NULL, 0, NULL, 0);
		} else {
			fuse_reply(self->fuse_dev_fd, ih->unique, -EOPNOTSUPP,
				   NULL, 0, NULL, 0);
		}
	}
	return NULL;
}

static void fuse_bpf_cleanup_fixture(struct _test_data_fuse_bpf *self)
{
	int prog_fd;

	if (self->mounted) {
		umount2(self->mnt_dir, MNT_DETACH);
		self->mounted = false;
	}
	close_fd(&self->stop_pipe[1]);
	if (self->thread_started) {
		pthread_join(self->daemon_tid, NULL);
		self->thread_started = false;
	}
	close_fd(&self->stop_pipe[0]);
	close_fd(&self->fuse_dev_fd);
	prog_fd = __atomic_exchange_n(&self->bpf_fd, -1, __ATOMIC_SEQ_CST);
	close_fd(&prog_fd);
	if (self->tmp_dir[0] != '\0') {
		rm_rf(self->tmp_dir);
		self->tmp_dir[0] = '\0';
	}
}

FIXTURE_SETUP(fuse_bpf)
{
	char feat[32] = { 0 }, opts[128];
	const char *base_tmp;
	int err, ret;

	memset(self, 0, sizeof(*self));
	self->fuse_dev_fd = -1;
	self->stop_pipe[0] = -1;
	self->stop_pipe[1] = -1;
	self->bpf_fd = -1;

	ret = read_sysfs_str(FUSE_BPF_FEAT_PATH, feat, sizeof(feat));
	if (ret == -ENOENT || ret == -EACCES)
		SKIP(return, "fuse-bpf not supported");
	ASSERT_EQ(0, ret);
	if (strcmp(feat, "supported\n") != 0)
		SKIP(return, "fuse-bpf not supported");

	if (unshare(CLONE_NEWNS) != 0 ||
	    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
		if (errno == EPERM || errno == EACCES || errno == EINVAL)
			SKIP(return, "CAP_SYS_ADMIN required");
		ASSERT_EQ(0, -errno);
	}

	base_tmp = access("/data/local/tmp", W_OK) == 0 ?
		   "/data/local/tmp" : "/tmp";
	snprintf(self->tmp_dir, sizeof(self->tmp_dir),
		 "%s/fuse_bpf_ksft.XXXXXX", base_tmp);
	ASSERT_TRUE(mkdtemp(self->tmp_dir) != NULL);

	snprintf(self->backing_dir, sizeof(self->backing_dir), "%s/backing",
		 self->tmp_dir);
	snprintf(self->mnt_dir, sizeof(self->mnt_dir), "%s/mnt", self->tmp_dir);
	if (mkdir(self->backing_dir, 0755) != 0 ||
	    mkdir(self->mnt_dir, 0755) != 0 ||
	    pipe2(self->stop_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
		err = -errno;
		goto err_cleanup;
	}

	self->fuse_dev_fd = open("/dev/fuse", O_RDWR | O_CLOEXEC);
	if (self->fuse_dev_fd < 0) {
		err = -errno;
		if (err == -ENOENT || err == -EACCES || err == -EPERM) {
			fuse_bpf_cleanup_fixture(self);
			SKIP(return, "/dev/fuse unavailable");
		}
		goto err_cleanup;
	}

	snprintf(opts, sizeof(opts),
		 "fd=%d,rootmode=040755,user_id=0,group_id=0",
		 self->fuse_dev_fd);
	if (mount("fuse", self->mnt_dir, "fuse", MS_NOSUID | MS_NODEV, opts)) {
		err = -errno;
		goto err_cleanup;
	}
	self->mounted = true;

	ret = pthread_create(&self->daemon_tid, NULL, fuse_mini_daemon, self);
	if (ret != 0) {
		err = -ret;
		goto err_cleanup;
	}
	self->thread_started = true;
	return;

err_cleanup:
	fuse_bpf_cleanup_fixture(self);
	ASSERT_EQ(0, err);
}

FIXTURE_TEARDOWN(fuse_bpf)
{
	fuse_bpf_cleanup_fixture(self);
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
	int bad_fd, prog_fd;

	ASSERT_EQ(0, read_sysfs_str(FUSE_BPF_FEAT_PATH, feat, sizeof(feat)));
	ASSERT_STREQ("supported\n", feat);

	prog_fd = load_fuse_bpf_prog(valid_prog, ARRAY_SIZE(valid_prog));
	if (prog_fd < 0 && (errno == EPERM || errno == EACCES))
		SKIP(return, "CAP_BPF required");
	ASSERT_GE(prog_fd, 0);
	close_fd(&prog_fd);

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

TEST_F(fuse_bpf, backed_file_rw_mmap)
{
	char fuse_file[PATH_MAX], back_file[PATH_MAX], unbacked_file[PATH_MAX];
	char buf[32] = { 0 };
	int bfd = -1, fd = -1, prog_fd;
	struct stat st;
	void *map;

	prog_fd = load_default_backing_prog();
	ASSERT_GE(prog_fd, 0);
	__atomic_store_n(&self->bpf_fd, prog_fd, __ATOMIC_SEQ_CST);

	snprintf(fuse_file, sizeof(fuse_file), "%s/backed/io.bin",
		 self->mnt_dir);
	snprintf(back_file, sizeof(back_file), "%s/io.bin", self->backing_dir);
	snprintf(unbacked_file, sizeof(unbacked_file), "%s/unbacked",
		 self->mnt_dir);

	fd = open(fuse_file, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0644);
	ASSERT_GE(fd, 0);
	ASSERT_EQ((ssize_t)14, write(fd, "hello-fuse-bpf", 14));
	ASSERT_EQ((ssize_t)14, pread(fd, buf, sizeof(buf) - 1, 0));
	EXPECT_STREQ("hello-fuse-bpf", buf);
	ASSERT_EQ(0, ftruncate(fd, 4096));

	map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	ASSERT_TRUE(map != MAP_FAILED);
	memcpy(map, "MMAP-BPF", 8);
	EXPECT_EQ(0, msync(map, 4096, MS_SYNC));
	EXPECT_EQ(0, munmap(map, 4096));
	close_fd(&fd);

	EXPECT_EQ(0U, __atomic_load_n(&self->rw_cnt, __ATOMIC_SEQ_CST));
	EXPECT_GE(__atomic_load_n(&self->lookup_cnt, __ATOMIC_SEQ_CST), 1U);

	bfd = open(back_file, O_RDONLY | O_CLOEXEC);
	ASSERT_GE(bfd, 0);
	ASSERT_EQ(0, fstat(bfd, &st));
	EXPECT_EQ((off_t)4096, st.st_size);
	memset(buf, 0, sizeof(buf));
	ASSERT_EQ((ssize_t)14, pread(bfd, buf, 14, 0));
	EXPECT_STREQ("MMAP-BPFse-bpf", buf);
	close_fd(&bfd);

	fd = open(unbacked_file, O_RDONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	memset(buf, 0, sizeof(buf));
	ASSERT_EQ((ssize_t)9, read(fd, buf, sizeof(buf) - 1));
	EXPECT_STREQ("fallback\n", buf);
	close_fd(&fd);
	EXPECT_EQ(1U, __atomic_load_n(&self->rw_cnt, __ATOMIC_SEQ_CST));
}

TEST_F(fuse_bpf, bpf_prefilter_policy_deny)
{
	const struct bpf_insn deny_unlink_prog[] = {
		FUSE_BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				 offsetof(struct fuse_bpf_args, opcode)),
		FUSE_BPF_JMP_IMM(BPF_JEQ, BPF_REG_2,
				 FUSE_UNLINK | FUSE_PREFILTER, 2),
		FUSE_BPF_MOV64_IMM(BPF_REG_0, FUSE_BPF_BACKING),
		FUSE_BPF_EXIT_INSN(),
		FUSE_BPF_MOV64_IMM(BPF_REG_0, -EPERM),
		FUSE_BPF_EXIT_INSN(),
	};
	char fuse_file[PATH_MAX], back_file[PATH_MAX];
	char buf[16] = { 0 };
	int bfd, fd, prog_fd;
	struct stat st;

	snprintf(back_file, sizeof(back_file), "%s/protected.txt",
		 self->backing_dir);
	snprintf(fuse_file, sizeof(fuse_file), "%s/backed/protected.txt",
		 self->mnt_dir);

	bfd = open(back_file, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
	ASSERT_GE(bfd, 0);
	ASSERT_EQ((ssize_t)8, write(bfd, "keep-me\n", 8));
	close_fd(&bfd);

	prog_fd = load_fuse_bpf_prog(deny_unlink_prog,
				     ARRAY_SIZE(deny_unlink_prog));
	ASSERT_GE(prog_fd, 0);
	__atomic_store_n(&self->bpf_fd, prog_fd, __ATOMIC_SEQ_CST);

	errno = 0;
	ASSERT_EQ(-1, unlink(fuse_file));
	EXPECT_EQ(EPERM, errno);
	EXPECT_EQ(0U, __atomic_load_n(&self->unlink_cnt, __ATOMIC_SEQ_CST));

	fd = open(fuse_file, O_RDONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	ASSERT_EQ((ssize_t)8, read(fd, buf, sizeof(buf) - 1));
	EXPECT_STREQ("keep-me\n", buf);
	close_fd(&fd);
	EXPECT_EQ(0U, __atomic_load_n(&self->rw_cnt, __ATOMIC_SEQ_CST));
	EXPECT_EQ(0, stat(back_file, &st));
}

TEST_F(fuse_bpf, no_daemon_mount)
{
	const char *tail = "rootmode=040755,user_id=0,group_id=0";
	char nd_back[256], nd_mnt[256];
	char mnt_file[PATH_MAX], back_file[PATH_MAX];
	char buf[32] = { 0 }, opts[160];
	int dir_fd = -1, fd = -1, null_fd = -1, prog_fd = -1, ret;
	struct stat st;

	snprintf(nd_back, sizeof(nd_back), "%s/nd_back", self->tmp_dir);
	snprintf(nd_mnt, sizeof(nd_mnt), "%s/nd_mnt", self->tmp_dir);
	ASSERT_EQ(0, mkdir(nd_back, 0755));
	ASSERT_EQ(0, mkdir(nd_mnt, 0755));

	dir_fd = open(nd_back, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(dir_fd, 0);
	prog_fd = load_default_backing_prog();
	ASSERT_GE(prog_fd, 0);
	if (fcntl(STDIN_FILENO, F_GETFD) < 0)
		null_fd = open("/dev/null", O_RDONLY);

	snprintf(opts, sizeof(opts), "no_daemon,root_dir=%d,root_bpf=-1,%s",
		 dir_fd, tail);
	errno = 0;
	EXPECT_EQ(-1, mount("fuse", nd_mnt, "fuse", MS_NOSUID | MS_NODEV,
			    opts));
	EXPECT_EQ(EINVAL, errno);

	snprintf(opts, sizeof(opts), "no_daemon,root_dir=%d,root_bpf=%d,%s",
		 dir_fd, dir_fd, tail);
	errno = 0;
	EXPECT_EQ(-1, mount("fuse", nd_mnt, "fuse", MS_NOSUID | MS_NODEV,
			    opts));
	EXPECT_EQ(EINVAL, errno);

	snprintf(opts, sizeof(opts), "no_daemon,root_dir=%d,root_bpf=%d,%s",
		 dir_fd, prog_fd, tail);
	ret = mount("fuse", nd_mnt, "fuse", MS_NOSUID | MS_NODEV, opts);
	close_fd(&dir_fd);
	close_fd(&prog_fd);
	close_fd(&null_fd);
	ASSERT_EQ(0, ret);

	snprintf(mnt_file, sizeof(mnt_file), "%s/nodm.txt", nd_mnt);
	snprintf(back_file, sizeof(back_file), "%s/nodm.txt", nd_back);

	fd = open(mnt_file, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0644);
	ASSERT_GE(fd, 0);
	ASSERT_EQ((ssize_t)12, write(fd, "no-daemon-ok", 12));
	ASSERT_EQ((ssize_t)12, pread(fd, buf, sizeof(buf) - 1, 0));
	EXPECT_STREQ("no-daemon-ok", buf);
	close_fd(&fd);

	ASSERT_EQ(0, stat(mnt_file, &st));
	EXPECT_EQ((off_t)12, st.st_size);
	ASSERT_EQ(0, stat(back_file, &st));
	EXPECT_EQ((off_t)12, st.st_size);
	EXPECT_EQ(0, umount2(nd_mnt, MNT_DETACH));
}

TEST_F(fuse_bpf, canonical_path_inotify)
{
	char fuse_file[PATH_MAX], back_file[PATH_MAX];
	uint64_t ev_buf[64];
	uint32_t mask = 0;
	struct pollfd pfd;
	int fd, ifd, prog_fd, wd;
	ssize_t len, off;

	prog_fd = load_default_backing_prog();
	ASSERT_GE(prog_fd, 0);
	__atomic_store_n(&self->bpf_fd, prog_fd, __ATOMIC_SEQ_CST);

	snprintf(fuse_file, sizeof(fuse_file), "%s/backed/watch.txt",
		 self->mnt_dir);
	snprintf(back_file, sizeof(back_file), "%s/watch.txt",
		 self->backing_dir);

	fd = open(fuse_file, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0644);
	ASSERT_GE(fd, 0);
	close_fd(&fd);

	ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	ASSERT_GE(ifd, 0);
	wd = inotify_add_watch(ifd, fuse_file, IN_MODIFY | IN_CLOSE_WRITE);
	ASSERT_GE(wd, 0);

	fd = open(fuse_file, O_WRONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	ASSERT_EQ((ssize_t)10, write(fd, "watch-data", 10));
	close_fd(&fd);

	pfd.fd = ifd;
	pfd.events = POLLIN;
	ASSERT_GT(poll(&pfd, 1, 2000), 0);
	len = read(ifd, ev_buf, sizeof(ev_buf));
	ASSERT_GE(len, (ssize_t)sizeof(struct inotify_event));

	for (off = 0; off < len; ) {
		const struct inotify_event *ev =
			(const void *)((const char *)ev_buf + off);

		if (ev->wd == wd)
			mask |= ev->mask;
		off += (ssize_t)sizeof(*ev) + (ssize_t)ev->len;
	}
	EXPECT_NE(0U, mask & IN_MODIFY);
	EXPECT_NE(0U, mask & IN_CLOSE_WRITE);

	mask = 0;
	fd = open(back_file, O_WRONLY | O_APPEND | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	ASSERT_EQ((ssize_t)6, write(fd, "-lower", 6));
	close_fd(&fd);

	ASSERT_GT(poll(&pfd, 1, 2000), 0);
	len = read(ifd, ev_buf, sizeof(ev_buf));
	ASSERT_GE(len, (ssize_t)sizeof(struct inotify_event));
	for (off = 0; off < len; ) {
		const struct inotify_event *ev =
			(const void *)((const char *)ev_buf + off);

		if (ev->wd == wd)
			mask |= ev->mask;
		off += (ssize_t)sizeof(*ev) + (ssize_t)ev->len;
	}
	EXPECT_NE(0U, mask & IN_MODIFY);
	EXPECT_NE(0U, mask & IN_CLOSE_WRITE);
	close_fd(&ifd);
}

TEST_HARNESS_MAIN
