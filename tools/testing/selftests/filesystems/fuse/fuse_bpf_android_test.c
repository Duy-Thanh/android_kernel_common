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
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/vfs.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <linux/android_fuse.h>
#include <linux/bpf.h>
#include <linux/fuse.h>

#include "../../kselftest_harness.h"

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE	(1U << 0)
#endif
#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE		(1U << 1)
#endif
#ifndef FUSE_SUPER_MAGIC
#define FUSE_SUPER_MAGIC	0x65735546
#endif
#ifndef FALLOC_FL_KEEP_SIZE
#define FALLOC_FL_KEEP_SIZE	0x01
#endif
#ifndef FALLOC_FL_PUNCH_HOLE
#define FALLOC_FL_PUNCH_HOLE	0x02
#endif
#ifndef SEEK_DATA
#define SEEK_DATA		3
#endif
#ifndef SEEK_HOLE
#define SEEK_HOLE		4
#endif

#define FUSE_BPF_FEAT_PATH	"/sys/fs/fuse/features/fuse_bpf"
#define FUSE_BPF_TYPE_PATH	"/sys/fs/fuse/bpf_prog_type_fuse"
#define FUSE_BACKED_NODEID	2ULL
#define FUSE_UNBACKED_NODEID	3ULL
#define FUSE_REDACTED_NODEID	4ULL
#define FUSE_ELOOP_NODEID	5ULL

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
#define FUSE_BPF_JMP32_IMM(OP, DST, IMM, OFF) \
	((struct bpf_insn){ .code = BPF_JMP32 | (OP) | BPF_K, \
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
	int prog_fd = __atomic_load_n(&self->bpf_fd, __ATOMIC_SEQ_CST);
	struct fuse_entry_out eo = { .entry_valid = 3600, .attr_valid = 3600 };
	struct fuse_entry_bpf_out bo = {
		.backing_action = FUSE_ACTION_REPLACE,
		.bpf_action = prog_fd >= 0 ?
			      FUSE_ACTION_REPLACE : FUSE_ACTION_KEEP,
		.bpf_fd = prog_fd >= 0 ? (uint64_t)(uint32_t)prog_fd : 0,
	};

	__atomic_add_fetch(&self->lookup_cnt, 1, __ATOMIC_SEQ_CST);
	if (ih->nodeid == FUSE_ROOT_ID && !strcmp(name, "backed")) {
		int dir_fd = open(self->backing_dir,
				  O_RDONLY | O_DIRECTORY | O_CLOEXEC);

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
	} else if (ih->nodeid == FUSE_ROOT_ID && !strcmp(name, "redacted")) {
		char rpath[PATH_MAX];
		struct stat st;
		int ffd;

		snprintf(rpath, sizeof(rpath), "%s/redacted",
			 self->backing_dir);
		ffd = open(rpath, O_RDWR | O_CLOEXEC);
		if (ffd < 0 || fstat(ffd, &st) != 0) {
			int err = -errno;

			close_fd(&ffd);
			fuse_reply(self->fuse_dev_fd, ih->unique, err,
				   NULL, 0, NULL, 0);
			return;
		}
		bo.backing_fd = (uint64_t)(uint32_t)ffd;
		eo.nodeid = FUSE_REDACTED_NODEID;
		eo.attr.ino = FUSE_REDACTED_NODEID;
		eo.attr.mode = S_IFREG | 0644;
		eo.attr.nlink = 1;
		eo.attr.size = (uint64_t)st.st_size;
		fuse_reply(self->fuse_dev_fd, ih->unique, 0,
			   &eo, sizeof(eo), &bo, sizeof(bo));
		close(ffd);
	} else if (ih->nodeid == FUSE_ROOT_ID && !strcmp(name, "eloop_node")) {
		int pfd = open("/proc/self/mounts", O_RDONLY | O_CLOEXEC);

		if (pfd < 0) {
			fuse_reply(self->fuse_dev_fd, ih->unique, -errno,
				   NULL, 0, NULL, 0);
			return;
		}
		bo.backing_fd = (uint64_t)(uint32_t)pfd;
		eo.nodeid = FUSE_ELOOP_NODEID;
		eo.attr.ino = FUSE_ELOOP_NODEID;
		eo.attr.mode = S_IFREG | 0444;
		eo.attr.nlink = 1;
		fuse_reply(self->fuse_dev_fd, ih->unique, 0,
			   &eo, sizeof(eo), &bo, sizeof(bo));
		close(pfd);
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
			    ih->nodeid == FUSE_UNBACKED_NODEID ||
			    ih->nodeid == FUSE_REDACTED_NODEID)) {
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
		} else if (op == FUSE_READ && (ih->opcode & FUSE_PREFILTER)) {
			fuse_reply(self->fuse_dev_fd, ih->unique, 0,
				   body, sizeof(struct fuse_read_in), NULL, 0);
		} else if (op == FUSE_READ || op == FUSE_WRITE) {
			const char *data = ih->nodeid == FUSE_REDACTED_NODEID ?
					   "redacted\n" : "fallback\n";
			bool ok = op == FUSE_READ &&
				  (ih->nodeid == FUSE_UNBACKED_NODEID ||
				   ih->nodeid == FUSE_REDACTED_NODEID);

			__atomic_add_fetch(&self->rw_cnt, 1, __ATOMIC_SEQ_CST);
			fuse_reply(self->fuse_dev_fd, ih->unique,
				   ok ? 0 : -EIO, data, 9, NULL, 0);
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

static int sys_renameat2(const char *oldp, const char *newp, unsigned int flags)
{
	return (int)syscall(__NR_renameat2, AT_FDCWD, oldp, AT_FDCWD, newp,
			    flags);
}

TEST_F(fuse_bpf, namespace_dir_and_rename)
{
	char f_a[PATH_MAX], f_b[PATH_MAX], f_c[PATH_MAX], back_b[PATH_MAX];
	char sub_dir[384], back_sub[384], buf[16] = { 0 };
	int fd, prog_fd;
	struct stat st;

	prog_fd = load_default_backing_prog();
	ASSERT_GE(prog_fd, 0);
	__atomic_store_n(&self->bpf_fd, prog_fd, __ATOMIC_SEQ_CST);

	snprintf(sub_dir, sizeof(sub_dir), "%s/backed/sub", self->mnt_dir);
	snprintf(back_sub, sizeof(back_sub), "%s/sub", self->backing_dir);
	snprintf(f_a, sizeof(f_a), "%s/a.txt", sub_dir);
	snprintf(f_b, sizeof(f_b), "%s/b.txt", sub_dir);
	snprintf(f_c, sizeof(f_c), "%s/c.txt", sub_dir);
	snprintf(back_b, sizeof(back_b), "%s/b.txt", back_sub);

	ASSERT_EQ(0, mkdir(sub_dir, 0755));
	ASSERT_EQ(0, stat(back_sub, &st));
	EXPECT_TRUE(S_ISDIR(st.st_mode));

	fd = open(f_a, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0644);
	ASSERT_GE(fd, 0);
	ASSERT_EQ((ssize_t)5, write(fd, "alpha", 5));
	close_fd(&fd);

	fd = open(f_c, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0644);
	ASSERT_GE(fd, 0);
	ASSERT_EQ((ssize_t)5, write(fd, "gamma", 5));
	close_fd(&fd);

	errno = 0;
	ASSERT_EQ(-1, rmdir(sub_dir));
	EXPECT_EQ(ENOTEMPTY, errno);

	ASSERT_EQ(0, rename(f_a, f_b));
	errno = 0;
	EXPECT_EQ(-1, stat(f_a, &st));
	EXPECT_EQ(ENOENT, errno);
	EXPECT_EQ(0, stat(back_b, &st));

	errno = 0;
	ASSERT_EQ(-1, sys_renameat2(f_b, f_c, RENAME_NOREPLACE));
	EXPECT_EQ(EEXIST, errno);

	ASSERT_EQ(0, sys_renameat2(f_b, f_c, RENAME_EXCHANGE));
	fd = open(f_b, O_RDONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	ASSERT_EQ((ssize_t)5, read(fd, buf, sizeof(buf) - 1));
	EXPECT_STREQ("gamma", buf);
	close_fd(&fd);

	memset(buf, 0, sizeof(buf));
	fd = open(f_c, O_RDONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	ASSERT_EQ((ssize_t)5, read(fd, buf, sizeof(buf) - 1));
	EXPECT_STREQ("alpha", buf);
	close_fd(&fd);

	ASSERT_EQ(0, unlink(f_b));
	ASSERT_EQ(0, unlink(f_c));
	ASSERT_EQ(0, rmdir(sub_dir));
	errno = 0;
	EXPECT_EQ(-1, stat(back_sub, &st));
	EXPECT_EQ(ENOENT, errno);
	EXPECT_EQ(0U, __atomic_load_n(&self->rw_cnt, __ATOMIC_SEQ_CST));
}

TEST_F(fuse_bpf, links_symlinks_setattr_statfs)
{
	struct timespec ts[2] = {
		{ .tv_sec = 1700000100, .tv_nsec = 0 },
		{ .tv_sec = 1700000200, .tv_nsec = 0 },
	};
	char src[PATH_MAX], hlink[PATH_MAX], slink[PATH_MAX];
	char lbuf[64] = { 0 };
	struct statfs sfs;
	int fd, prog_fd;
	struct stat st;

	prog_fd = load_default_backing_prog();
	ASSERT_GE(prog_fd, 0);
	__atomic_store_n(&self->bpf_fd, prog_fd, __ATOMIC_SEQ_CST);

	snprintf(src, sizeof(src), "%s/backed/orig.txt", self->mnt_dir);
	snprintf(hlink, sizeof(hlink), "%s/backed/hard.txt", self->mnt_dir);
	snprintf(slink, sizeof(slink), "%s/backed/sym.lnk", self->mnt_dir);

	fd = open(src, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0644);
	ASSERT_GE(fd, 0);
	ASSERT_EQ((ssize_t)9, write(fd, "link-data", 9));
	close_fd(&fd);

	ASSERT_EQ(0, link(src, hlink));
	ASSERT_EQ(0, stat(src, &st));
	EXPECT_EQ(2UL, (unsigned long)st.st_nlink);

	ASSERT_EQ(0, symlink("orig.txt", slink));
	ASSERT_EQ((ssize_t)8, readlink(slink, lbuf, sizeof(lbuf) - 1));
	EXPECT_STREQ("orig.txt", lbuf);
	ASSERT_EQ(0, lstat(slink, &st));
	EXPECT_TRUE(S_ISLNK(st.st_mode));

	ASSERT_EQ(0, chmod(src, 0600));
	ASSERT_EQ(0, truncate(src, 4));
	ASSERT_EQ(0, utimensat(AT_FDCWD, src, ts, 0));

	ASSERT_EQ(0, stat(hlink, &st));
	EXPECT_EQ(0600U, (unsigned int)(st.st_mode & 0777));
	EXPECT_EQ((off_t)4, st.st_size);
	EXPECT_EQ(1700000200L, (long)st.st_mtime);

	ASSERT_EQ(0, statfs(src, &sfs));
	EXPECT_EQ((unsigned long)FUSE_SUPER_MAGIC, (unsigned long)sfs.f_type);
}

TEST_F(fuse_bpf, xattr_operations)
{
	const char *xname = "user.fusebpf_test";
	char fuse_file[PATH_MAX], back_file[PATH_MAX];
	char val[32] = { 0 }, list[256] = { 0 };
	int fd, prog_fd, ret;
	bool found = false;
	ssize_t len, off;

	prog_fd = load_default_backing_prog();
	ASSERT_GE(prog_fd, 0);
	__atomic_store_n(&self->bpf_fd, prog_fd, __ATOMIC_SEQ_CST);

	snprintf(fuse_file, sizeof(fuse_file), "%s/backed/xattr.txt",
		 self->mnt_dir);
	snprintf(back_file, sizeof(back_file), "%s/xattr.txt",
		 self->backing_dir);

	fd = open(fuse_file, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0644);
	ASSERT_GE(fd, 0);
	close_fd(&fd);

	ret = setxattr(fuse_file, xname, "xattr-val", 9, 0);
	if (ret < 0 && (errno == EOPNOTSUPP || errno == ENOTSUP))
		SKIP(return, "user xattr unsupported on backing fs");
	ASSERT_EQ(0, ret);

	ASSERT_EQ((ssize_t)9, getxattr(fuse_file, xname, val, sizeof(val) - 1));
	EXPECT_STREQ("xattr-val", val);

	memset(val, 0, sizeof(val));
	ASSERT_EQ((ssize_t)9, getxattr(back_file, xname, val, sizeof(val) - 1));
	EXPECT_STREQ("xattr-val", val);

	len = listxattr(fuse_file, list, sizeof(list));
	ASSERT_GT(len, (ssize_t)0);
	for (off = 0; off < len;
	     off += (ssize_t)strlen(list + off) + 1) {
		if (!strcmp(list + off, xname))
			found = true;
	}
	EXPECT_TRUE(found);

	ASSERT_EQ(0, removexattr(fuse_file, xname));
	errno = 0;
	EXPECT_EQ((ssize_t)-1, getxattr(fuse_file, xname, val, sizeof(val)));
	EXPECT_EQ(ENODATA, errno);
}

struct excl_race_ctx {
	const char *path;
	uint32_t ready;
	uint32_t go;
	uint32_t wins;
	uint32_t exists;
	uint32_t errs;
};

static void *excl_race_worker(void *arg)
{
	struct excl_race_ctx *ctx = arg;
	int fd;

	__atomic_add_fetch(&ctx->ready, 1, __ATOMIC_SEQ_CST);
	while (!__atomic_load_n(&ctx->go, __ATOMIC_ACQUIRE))
		sched_yield();

	fd = open(ctx->path, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0644);
	if (fd >= 0) {
		__atomic_add_fetch(&ctx->wins, 1, __ATOMIC_SEQ_CST);
		close(fd);
	} else if (errno == EEXIST) {
		__atomic_add_fetch(&ctx->exists, 1, __ATOMIC_SEQ_CST);
	} else {
		__atomic_add_fetch(&ctx->errs, 1, __ATOMIC_SEQ_CST);
	}
	return NULL;
}

TEST_F(fuse_bpf, readdir_and_excl_race)
{
	char f1[PATH_MAX], f2[PATH_MAX], race_f[PATH_MAX];
	bool saw_one = false, saw_two = false;
	struct excl_race_ctx ctx = { 0 };
	char backed_dir[384];
	int fd, i, prog_fd;
	struct dirent *de;
	pthread_t tids[8];
	DIR *dir;

	prog_fd = load_default_backing_prog();
	ASSERT_GE(prog_fd, 0);
	__atomic_store_n(&self->bpf_fd, prog_fd, __ATOMIC_SEQ_CST);

	snprintf(backed_dir, sizeof(backed_dir), "%s/backed", self->mnt_dir);
	snprintf(f1, sizeof(f1), "%s/d_one", backed_dir);
	snprintf(f2, sizeof(f2), "%s/d_two", backed_dir);
	snprintf(race_f, sizeof(race_f), "%s/excl_race", backed_dir);

	fd = open(f1, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0644);
	ASSERT_GE(fd, 0);
	close_fd(&fd);
	fd = open(f2, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0644);
	ASSERT_GE(fd, 0);
	close_fd(&fd);

	dir = opendir(backed_dir);
	ASSERT_TRUE(dir != NULL);
	while ((de = readdir(dir)) != NULL) {
		if (!strcmp(de->d_name, "d_one"))
			saw_one = true;
		else if (!strcmp(de->d_name, "d_two"))
			saw_two = true;
	}
	closedir(dir);
	EXPECT_TRUE(saw_one);
	EXPECT_TRUE(saw_two);

	ctx.path = race_f;
	for (i = 0; i < 8; i++)
		ASSERT_EQ(0, pthread_create(&tids[i], NULL, excl_race_worker,
					    &ctx));
	while (__atomic_load_n(&ctx.ready, __ATOMIC_SEQ_CST) < 8U)
		sched_yield();
	__atomic_store_n(&ctx.go, 1, __ATOMIC_RELEASE);
	for (i = 0; i < 8; i++)
		ASSERT_EQ(0, pthread_join(tids[i], NULL));

	EXPECT_EQ(1U, ctx.wins);
	EXPECT_EQ(7U, ctx.exists);
	EXPECT_EQ(0U, ctx.errs);
}

TEST_F(fuse_bpf, bpf_postfilter_attr_and_err_rewrite)
{
	const struct bpf_insn postfilter_prog[] = {
		/* 0..4: dispatch GETATTR/UNLINK prefilter & postfilter */
		FUSE_BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				 offsetof(struct fuse_bpf_args, opcode)),
		FUSE_BPF_JMP_IMM(BPF_JEQ, BPF_REG_2,
				 FUSE_GETATTR | FUSE_PREFILTER, 5),
		FUSE_BPF_JMP_IMM(BPF_JEQ, BPF_REG_2,
				 FUSE_UNLINK | FUSE_PREFILTER, 4),
		FUSE_BPF_JMP_IMM(BPF_JEQ, BPF_REG_2,
				 FUSE_GETATTR | FUSE_POSTFILTER, 5),
		FUSE_BPF_JMP_IMM(BPF_JEQ, BPF_REG_2,
				 FUSE_UNLINK | FUSE_POSTFILTER, 9),
		/* 5..6: default -> FUSE_BPF_BACKING */
		FUSE_BPF_MOV64_IMM(BPF_REG_0, FUSE_BPF_BACKING),
		FUSE_BPF_EXIT_INSN(),
		/* 7..8: GETATTR/UNLINK prefilter -> BACKING | POST_FILTER */
		FUSE_BPF_MOV64_IMM(BPF_REG_0,
				   FUSE_BPF_BACKING | FUSE_BPF_POST_FILTER),
		FUSE_BPF_EXIT_INSN(),
		/* 9..13: GETATTR postfilter -> rewrite attr.size = 77777 */
		FUSE_BPF_LDX_MEM(BPF_DW, BPF_REG_3, BPF_REG_1,
				 offsetof(struct fuse_bpf_args,
					  out_args[0].value)),
		FUSE_BPF_MOV64_IMM(BPF_REG_4, 77777),
		FUSE_BPF_STX_MEM(BPF_DW, BPF_REG_3, BPF_REG_4,
				 offsetof(struct fuse_attr_out, attr.size)),
		FUSE_BPF_MOV64_IMM(BPF_REG_0, 0),
		FUSE_BPF_EXIT_INSN(),
		/* 14..19: UNLINK postfilter -> rewrite -ENOENT to -EXDEV */
		FUSE_BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				 offsetof(struct fuse_bpf_args, error_in)),
		FUSE_BPF_JMP32_IMM(BPF_JEQ, BPF_REG_3, -ENOENT, 2),
		FUSE_BPF_MOV64_IMM(BPF_REG_0, 0),
		FUSE_BPF_EXIT_INSN(),
		FUSE_BPF_MOV64_IMM(BPF_REG_0, -EXDEV),
		FUSE_BPF_EXIT_INSN(),
	};
	char fuse_file[PATH_MAX], fuse_moved[PATH_MAX];
	char back_file[PATH_MAX], back_moved[PATH_MAX];
	int bfd, fd = -1, prog_fd;
	struct stat st;

	snprintf(back_file, sizeof(back_file), "%s/post.txt",
		 self->backing_dir);
	snprintf(back_moved, sizeof(back_moved), "%s/moved.txt",
		 self->backing_dir);
	snprintf(fuse_file, sizeof(fuse_file), "%s/backed/post.txt",
		 self->mnt_dir);
	snprintf(fuse_moved, sizeof(fuse_moved), "%s/backed/moved.txt",
		 self->mnt_dir);

	bfd = open(back_file, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
	ASSERT_GE(bfd, 0);
	ASSERT_EQ((ssize_t)11, write(bfd, "eleven-byte", 11));
	close_fd(&bfd);

	prog_fd = load_fuse_bpf_prog(postfilter_prog,
				     ARRAY_SIZE(postfilter_prog));
	ASSERT_GE(prog_fd, 0);
	__atomic_store_n(&self->bpf_fd, prog_fd, __ATOMIC_SEQ_CST);

	/*
	 * Pin the FUSE dentry with an open fd so fuse_dentry_delete() does
	 * not evict it before unlink() reaches fuse_unlink_backing().
	 */
	fd = open(fuse_file, O_RDONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);

	ASSERT_EQ(0, stat(fuse_file, &st));
	EXPECT_EQ((off_t)77777, st.st_size);
	ASSERT_EQ(0, stat(back_file, &st));
	EXPECT_EQ((off_t)11, st.st_size);

	ASSERT_EQ(0, rename(back_file, back_moved));
	errno = 0;
	ASSERT_EQ(-1, unlink(fuse_file));
	EXPECT_EQ(EXDEV, errno);
	close_fd(&fd);
	ASSERT_EQ(0, unlink(fuse_moved));
}

TEST_F(fuse_bpf, bpf_user_filter_daemon_fallback)
{
	const struct bpf_insn redact_prog[] = {
		FUSE_BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				 offsetof(struct fuse_bpf_args, opcode)),
		FUSE_BPF_JMP_IMM(BPF_JEQ, BPF_REG_2,
				 FUSE_READ | FUSE_PREFILTER, 2),
		FUSE_BPF_MOV64_IMM(BPF_REG_0, FUSE_BPF_BACKING),
		FUSE_BPF_EXIT_INSN(),
		FUSE_BPF_MOV64_IMM(BPF_REG_0, FUSE_BPF_USER_FILTER),
		FUSE_BPF_EXIT_INSN(),
	};
	char fuse_file[PATH_MAX], back_file[PATH_MAX];
	char buf[16] = { 0 };
	int bfd, fd, prog_fd;
	struct stat st;

	snprintf(back_file, sizeof(back_file), "%s/redacted",
		 self->backing_dir);
	snprintf(fuse_file, sizeof(fuse_file), "%s/redacted", self->mnt_dir);

	bfd = open(back_file, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0644);
	ASSERT_GE(bfd, 0);
	close_fd(&bfd);

	prog_fd = load_fuse_bpf_prog(redact_prog, ARRAY_SIZE(redact_prog));
	ASSERT_GE(prog_fd, 0);
	__atomic_store_n(&self->bpf_fd, prog_fd, __ATOMIC_SEQ_CST);

	fd = open(fuse_file, O_RDWR | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	ASSERT_EQ((ssize_t)9, write(fd, "secret-9\n", 9));
	EXPECT_EQ(0U, __atomic_load_n(&self->rw_cnt, __ATOMIC_SEQ_CST));
	ASSERT_EQ(0, stat(fuse_file, &st));
	EXPECT_EQ((off_t)9, st.st_size);

	ASSERT_EQ((ssize_t)9, pread(fd, buf, sizeof(buf) - 1, 0));
	EXPECT_STREQ("redacted\n", buf);
	EXPECT_GE(__atomic_load_n(&self->rw_cnt, __ATOMIC_SEQ_CST), 1U);
	close_fd(&fd);

	memset(buf, 0, sizeof(buf));
	bfd = open(back_file, O_RDONLY | O_CLOEXEC);
	ASSERT_GE(bfd, 0);
	ASSERT_EQ((ssize_t)9, read(bfd, buf, sizeof(buf) - 1));
	EXPECT_STREQ("secret-9\n", buf);
	close_fd(&bfd);
}

TEST_F(fuse_bpf, stacking_depth_eloop)
{
	const char *tail = "rootmode=040755,user_id=0,group_id=0";
	int fuse_dir_fd = -1, null_fd = -1, proc_fd = -1, prog_fd;
	char eloop_path[PATH_MAX], st_mnt[256], opts[160];
	struct utsname uts;
	struct stat st;

	snprintf(st_mnt, sizeof(st_mnt), "%s/st_mnt", self->tmp_dir);
	ASSERT_EQ(0, mkdir(st_mnt, 0755));

	prog_fd = load_default_backing_prog();
	ASSERT_GE(prog_fd, 0);
	if (fcntl(STDIN_FILENO, F_GETFD) < 0)
		null_fd = open("/dev/null", O_RDONLY);

	proc_fd = open("/proc", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(proc_fd, 0);
	snprintf(opts, sizeof(opts), "no_daemon,root_dir=%d,root_bpf=%d,%s",
		 proc_fd, prog_fd, tail);
	errno = 0;
	EXPECT_EQ(-1, mount("fuse", st_mnt, "fuse", MS_NOSUID | MS_NODEV,
			    opts));
	EXPECT_EQ(EINVAL, errno);
	close_fd(&proc_fd);

	fuse_dir_fd = open(self->mnt_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(fuse_dir_fd, 0);
	snprintf(opts, sizeof(opts), "no_daemon,root_dir=%d,root_bpf=%d,%s",
		 fuse_dir_fd, prog_fd, tail);
	errno = 0;
	EXPECT_EQ(-1, mount("fuse", st_mnt, "fuse", MS_NOSUID | MS_NODEV,
			    opts));
	EXPECT_EQ(EINVAL, errno);
	close_fd(&fuse_dir_fd);
	close_fd(&null_fd);

	__atomic_store_n(&self->bpf_fd, prog_fd, __ATOMIC_SEQ_CST);
	ASSERT_EQ(0, uname(&uts));
	if ((strncmp(uts.release, "6.18.", 5) != 0 &&
	     strncmp(uts.release, "6.12.", 5) != 0) ||
	    getenv("FUSE_BPF_TEST_LOOKUP_ELOOP") != NULL) {
		snprintf(eloop_path, sizeof(eloop_path), "%s/eloop_node",
			 self->mnt_dir);
		errno = 0;
		EXPECT_EQ(-1, stat(eloop_path, &st));
		EXPECT_EQ(ELOOP, errno);
	}
}

TEST_F(fuse_bpf, file_ops_lseek_flock_fallocate)
{
	char fuse_file[PATH_MAX];
	int fd1 = -1, fd2 = -1, prog_fd;
	long psize;

	prog_fd = load_default_backing_prog();
	ASSERT_GE(prog_fd, 0);
	__atomic_store_n(&self->bpf_fd, prog_fd, __ATOMIC_SEQ_CST);

	psize = sysconf(_SC_PAGESIZE);
	if (psize <= 0)
		psize = 4096;

	snprintf(fuse_file, sizeof(fuse_file), "%s/backed/fops.bin",
		 self->mnt_dir);
	fd1 = open(fuse_file, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0644);
	ASSERT_GE(fd1, 0);

	ASSERT_EQ(0, fallocate(fd1, 0, 0, (off_t)(psize * 2)));
	ASSERT_EQ((ssize_t)9, pwrite(fd1, "data-blk!", 9, (off_t)psize));
	ASSERT_EQ(0, fallocate(fd1, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
			       0, (off_t)psize));

	EXPECT_EQ((off_t)0, lseek(fd1, 0, SEEK_HOLE));
	EXPECT_EQ((off_t)psize, lseek(fd1, 0, SEEK_DATA));
	EXPECT_EQ((off_t)(psize * 2), lseek(fd1, 0, SEEK_END));

	fd2 = open(fuse_file, O_RDWR | O_CLOEXEC);
	ASSERT_GE(fd2, 0);
	ASSERT_EQ(0, flock(fd1, LOCK_EX | LOCK_NB));
	errno = 0;
	EXPECT_EQ(-1, flock(fd2, LOCK_EX | LOCK_NB));
	EXPECT_EQ(EWOULDBLOCK, errno);
	EXPECT_EQ(0, flock(fd1, LOCK_UN));
	EXPECT_EQ(0, flock(fd2, LOCK_EX | LOCK_NB));
	EXPECT_EQ(0, flock(fd2, LOCK_UN));
	close_fd(&fd2);
	close_fd(&fd1);
}

TEST_HARNESS_MAIN
