// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Google LLC
 *
 * Selftests for the Android BPF FD Loader driver (/dev/android_bpf_loader).
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/bpf.h>
#include <linux/elf.h>
#include <linux/android/bpf_fd_loader.h>

#include "kselftest_harness.h"
#include "test_loader_lskel.h"

#ifndef BPF_ALU64
#define BPF_ALU64 0x07
#endif
#ifndef BPF_MOV
#define BPF_MOV 0xb0
#endif
#ifndef BPF_K
#define BPF_K 0x00
#endif
#ifndef BPF_ST
#define BPF_ST 0x62
#endif
#ifndef BPF_MEM
#define BPF_MEM 0x60
#endif
#ifndef BPF_JMP
#define BPF_JMP 0x05
#endif
#ifndef BPF_EXIT
#define BPF_EXIT 0x90
#endif
#ifndef BPF_W
#define BPF_W 0x00
#endif
#ifndef BPF_REG_0
#define BPF_REG_0 0
#endif
#ifndef BPF_REG_1
#define BPF_REG_1 1
#endif

#ifndef BPF_MOV64_IMM
#define BPF_MOV64_IMM(DST, IMM) \
	((struct bpf_insn) { \
		.code    = BPF_ALU64 | BPF_MOV | BPF_K, \
		.dst_reg = (DST), \
		.src_reg = 0, \
		.off     = 0, \
		.imm     = (IMM) \
	})
#endif

#ifndef BPF_ST_MEM
#define BPF_ST_MEM(SIZE, DST, OFF, IMM) \
	((struct bpf_insn) { \
		.code    = BPF_ST | (SIZE) | BPF_MEM, \
		.dst_reg = (DST), \
		.src_reg = 0, \
		.off     = (OFF), \
		.imm     = (IMM) \
	})
#endif

#ifndef BPF_EXIT_INSN
#define BPF_EXIT_INSN() \
	((struct bpf_insn) { \
		.code    = BPF_JMP | BPF_EXIT, \
		.dst_reg = 0, \
		.src_reg = 0, \
		.off     = 0, \
		.imm     = 0 \
	})
#endif

struct test_ctx {
	__u32 magic;
	__u32 result;
};

static const char shstrtab_data[] = "\0__loader.prog\0__loader.map\0license\0.shstrtab";
#define SHSTRTAB_PROG_OFF 1
#define SHSTRTAB_MAP_OFF 15
#define SHSTRTAB_LICENSE_OFF 28
#define SHSTRTAB_SHSTRTAB_OFF 36

struct elf_builder_opts {
	const void *insns;
	size_t insns_sz;
	const void *map_data;
	size_t map_data_sz;
	const char *license;
	size_t license_sz;
	bool omit_prog;
	bool omit_map;
	bool omit_license;
	bool corrupt_magic;
	bool corrupt_machine;
	bool corrupt_class;
	bool corrupt_type;
	bool corrupt_shentsize;
	bool corrupt_shstrndx;
	bool empty_map;
	bool empty_license;
	bool unaligned_prog;
	bool non_null_terminated_license;
};

static inline size_t align8(size_t n)
{
	return (n + 7) & ~((size_t)7);
}

static int create_loader_elf_ex(const struct elf_builder_opts *opts)
{
	Elf64_Shdr sechdrs[16];
	size_t shnum = 0;
	size_t cur_off = sizeof(Elf64_Ehdr);

	memset(sechdrs, 0, sizeof(sechdrs));

	/* Section 0: SHT_NULL */
	shnum++;

	/* Section 1: __loader.prog */
	size_t prog_idx = 0;
	size_t prog_off = 0;
	size_t prog_sz = 0;
	if (!opts->omit_prog && opts->insns && opts->insns_sz > 0) {
		prog_idx = shnum++;
		prog_off = align8(cur_off);
		prog_sz = opts->insns_sz;
		cur_off = prog_off + prog_sz;

		sechdrs[prog_idx].sh_name = SHSTRTAB_PROG_OFF;
		sechdrs[prog_idx].sh_type = SHT_PROGBITS;
		sechdrs[prog_idx].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
		sechdrs[prog_idx].sh_offset = prog_off;
		sechdrs[prog_idx].sh_size = prog_sz;
		sechdrs[prog_idx].sh_addralign = 8;
	}

	/* Section 2: __loader.map */
	size_t map_idx = 0;
	size_t map_off = 0;
	size_t map_sz = 0;
	if (!opts->omit_map && !opts->empty_map && opts->map_data && opts->map_data_sz > 0) {
		map_idx = shnum++;
		map_off = align8(cur_off);
		map_sz = opts->map_data_sz;
		cur_off = map_off + map_sz;

		sechdrs[map_idx].sh_name = SHSTRTAB_MAP_OFF;
		sechdrs[map_idx].sh_type = SHT_PROGBITS;
		sechdrs[map_idx].sh_flags = SHF_ALLOC;
		sechdrs[map_idx].sh_offset = map_off;
		sechdrs[map_idx].sh_size = map_sz;
		sechdrs[map_idx].sh_addralign = 8;
	} else if (opts->empty_map) {
		map_idx = shnum++;
		map_off = align8(cur_off);
		map_sz = 0;

		sechdrs[map_idx].sh_name = SHSTRTAB_MAP_OFF;
		sechdrs[map_idx].sh_type = SHT_PROGBITS;
		sechdrs[map_idx].sh_flags = SHF_ALLOC;
		sechdrs[map_idx].sh_offset = map_off;
		sechdrs[map_idx].sh_size = 0;
		sechdrs[map_idx].sh_addralign = 8;
	}

	/* Section 3: license */
	size_t lic_idx = 0;
	size_t lic_off = 0;
	size_t lic_sz = 0;
	if (!opts->omit_license && !opts->empty_license && opts->license && opts->license_sz > 0) {
		lic_idx = shnum++;
		lic_off = align8(cur_off);
		lic_sz = opts->license_sz;
		cur_off = lic_off + lic_sz;

		sechdrs[lic_idx].sh_name = SHSTRTAB_LICENSE_OFF;
		sechdrs[lic_idx].sh_type = SHT_PROGBITS;
		sechdrs[lic_idx].sh_flags = 0;
		sechdrs[lic_idx].sh_offset = lic_off;
		sechdrs[lic_idx].sh_size = lic_sz;
		sechdrs[lic_idx].sh_addralign = 1;
	} else if (opts->empty_license) {
		lic_idx = shnum++;
		lic_off = align8(cur_off);
		lic_sz = 0;

		sechdrs[lic_idx].sh_name = SHSTRTAB_LICENSE_OFF;
		sechdrs[lic_idx].sh_type = SHT_PROGBITS;
		sechdrs[lic_idx].sh_flags = 0;
		sechdrs[lic_idx].sh_offset = lic_off;
		sechdrs[lic_idx].sh_size = 0;
		sechdrs[lic_idx].sh_addralign = 1;
	}

	/* Section 4: .shstrtab */
	size_t shstr_idx = shnum++;
	size_t shstr_off = align8(cur_off);
	size_t shstr_sz = sizeof(shstrtab_data);
	cur_off = shstr_off + shstr_sz;

	sechdrs[shstr_idx].sh_name = SHSTRTAB_SHSTRTAB_OFF;
	sechdrs[shstr_idx].sh_type = SHT_STRTAB;
	sechdrs[shstr_idx].sh_flags = 0;
	sechdrs[shstr_idx].sh_offset = shstr_off;
	sechdrs[shstr_idx].sh_size = shstr_sz;
	sechdrs[shstr_idx].sh_addralign = 1;

	size_t sh_table_off = align8(cur_off);
	size_t total_file_sz = sh_table_off + shnum * sizeof(Elf64_Shdr);

	char *buf = calloc(1, total_file_sz);
	if (!buf)
		return -1;

	Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;
	ehdr->e_ident[EI_MAG0] = opts->corrupt_magic ? 0 : ELFMAG0;
	ehdr->e_ident[EI_MAG1] = ELFMAG1;
	ehdr->e_ident[EI_MAG2] = ELFMAG2;
	ehdr->e_ident[EI_MAG3] = ELFMAG3;
	ehdr->e_ident[EI_CLASS] = opts->corrupt_class ? ELFCLASS32 : ELFCLASS64;
	ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_type = opts->corrupt_type ? ET_EXEC : ET_REL;
	ehdr->e_machine = opts->corrupt_machine ? EM_NONE : EM_BPF;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_shoff = sh_table_off;
	ehdr->e_ehsize = sizeof(Elf64_Ehdr);
	ehdr->e_shentsize = opts->corrupt_shentsize ? sizeof(Elf64_Shdr) + 4 : sizeof(Elf64_Shdr);
	ehdr->e_shnum = shnum;
	ehdr->e_shstrndx = opts->corrupt_shstrndx ? (shnum + 1) : shstr_idx;

	if (prog_idx)
		memcpy(buf + prog_off, opts->insns, prog_sz);
	if (map_idx && map_sz > 0)
		memcpy(buf + map_off, opts->map_data, map_sz);
	if (lic_idx && lic_sz > 0)
		memcpy(buf + lic_off, opts->license, lic_sz);

	memcpy(buf + shstr_off, shstrtab_data, shstr_sz);
	memcpy(buf + sh_table_off, sechdrs, shnum * sizeof(Elf64_Shdr));

	int fd = memfd_create("loader_elf", 0);
	if (fd < 0) {
		free(buf);
		return -1;
	}

	ssize_t written = write(fd, buf, total_file_sz);
	free(buf);

	if (written != (ssize_t)total_file_sz) {
		close(fd);
		return -1;
	}

	lseek(fd, 0, SEEK_SET);
	return fd;
}

static int create_valid_loader_elf(const void *insns, size_t insns_sz,
				   const void *map_data, size_t map_data_sz,
				   const char *license, size_t license_sz)
{
	struct elf_builder_opts opts = {
		.insns = insns,
		.insns_sz = insns_sz,
		.map_data = map_data,
		.map_data_sz = map_data_sz,
		.license = license,
		.license_sz = license_sz,
	};
	return create_loader_elf_ex(&opts);
}

static int open_dev_or_skip(struct __test_metadata *_metadata)
{
	int fd = open("/dev/android_bpf_loader", O_RDWR);
	if (fd < 0) {
		if (errno == ENOENT || errno == ENODEV) {
			SKIP(return -1, "/dev/android_bpf_loader device not found");
		}
		if (errno == EACCES || errno == EPERM) {
			SKIP(return -1, "Permission denied opening /dev/android_bpf_loader");
		}
		ASSERT_GE(fd, 0);
	}
	return fd;
}

TEST(ioctl_ping)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_PING);
	ASSERT_EQ(ret, 0);

	close(dev_fd);
}

TEST(ioctl_invalid_cmd)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	int ret = ioctl(dev_fd, _IO('B', 99));
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOTTY);

	close(dev_fd);
}

TEST(invalid_loader_fd)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct test_ctx ctx = { .magic = 0xdeadbeef };
	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = (uint32_t)-1,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EINVAL);

	close(dev_fd);
}

TEST(closed_loader_fd)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	int dummy_fd = memfd_create("dummy", 0);
	ASSERT_GE(dummy_fd, 0);
	close(dummy_fd);

	struct test_ctx ctx = { .magic = 0xdeadbeef };
	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = (uint32_t)dummy_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EINVAL);

	close(dev_fd);
}

TEST(zero_ctx_size)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char map_data[] = "data";
	char license[] = "GPL";
	struct test_ctx ctx = { .magic = 0xdeadbeef };
	int elf_fd = create_valid_loader_elf(insns, sizeof(insns),
					     map_data, sizeof(map_data),
					     license, sizeof(license));
	ASSERT_GE(elf_fd, 0);

	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = 0,
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EINVAL);

	close(elf_fd);
	close(dev_fd);
}

TEST(oversized_ctx_size)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char map_data[] = "data";
	char license[] = "GPL";
	struct test_ctx ctx = { .magic = 0xdeadbeef };
	int elf_fd = create_valid_loader_elf(insns, sizeof(insns),
					     map_data, sizeof(map_data),
					     license, sizeof(license));
	ASSERT_GE(elf_fd, 0);

	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = 65536U,
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EINVAL);

	close(elf_fd);
	close(dev_fd);
}

TEST(invalid_ctx_ptr)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char map_data[] = "data";
	char license[] = "GPL";
	int elf_fd = create_valid_loader_elf(insns, sizeof(insns),
					     map_data, sizeof(map_data),
					     license, sizeof(license));
	ASSERT_GE(elf_fd, 0);

	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = sizeof(struct test_ctx),
		.ctx = 0x1,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EFAULT);

	close(elf_fd);
	close(dev_fd);
}

TEST(invalid_elf_garbage)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	int garbage_fd = memfd_create("garbage", 0);
	ASSERT_GE(garbage_fd, 0);

	char garbage[] = "This is not a valid ELF file header or binary.";
	ASSERT_EQ(write(garbage_fd, garbage, sizeof(garbage)), (ssize_t)sizeof(garbage));
	lseek(garbage_fd, 0, SEEK_SET);

	struct test_ctx ctx = { .magic = 0xdeadbeef };
	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = garbage_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOEXEC);

	close(garbage_fd);
	close(dev_fd);
}

TEST(corrupt_elf_magic)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char map_data[] = "data";
	char license[] = "GPL";
	struct elf_builder_opts opts = {
		.insns = insns,
		.insns_sz = sizeof(insns),
		.map_data = map_data,
		.map_data_sz = sizeof(map_data),
		.license = license,
		.license_sz = sizeof(license),
		.corrupt_magic = true,
	};
	int elf_fd = create_loader_elf_ex(&opts);
	ASSERT_GE(elf_fd, 0);

	struct test_ctx ctx = { .magic = 0xdeadbeef };
	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOEXEC);

	close(elf_fd);
	close(dev_fd);
}

TEST(corrupt_elf_class32)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char map_data[] = "data";
	char license[] = "GPL";
	struct elf_builder_opts opts = {
		.insns = insns,
		.insns_sz = sizeof(insns),
		.map_data = map_data,
		.map_data_sz = sizeof(map_data),
		.license = license,
		.license_sz = sizeof(license),
		.corrupt_class = true,
	};
	int elf_fd = create_loader_elf_ex(&opts);
	ASSERT_GE(elf_fd, 0);

	struct test_ctx ctx = { .magic = 0xdeadbeef };
	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOEXEC);

	close(elf_fd);
	close(dev_fd);
}

TEST(corrupt_elf_machine)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char map_data[] = "data";
	char license[] = "GPL";
	struct elf_builder_opts opts = {
		.insns = insns,
		.insns_sz = sizeof(insns),
		.map_data = map_data,
		.map_data_sz = sizeof(map_data),
		.license = license,
		.license_sz = sizeof(license),
		.corrupt_machine = true,
	};
	int elf_fd = create_loader_elf_ex(&opts);
	ASSERT_GE(elf_fd, 0);

	struct test_ctx ctx = { .magic = 0xdeadbeef };
	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOEXEC);

	close(elf_fd);
	close(dev_fd);
}

TEST(corrupt_elf_type)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char map_data[] = "data";
	char license[] = "GPL";
	struct elf_builder_opts opts = {
		.insns = insns,
		.insns_sz = sizeof(insns),
		.map_data = map_data,
		.map_data_sz = sizeof(map_data),
		.license = license,
		.license_sz = sizeof(license),
		.corrupt_type = true,
	};
	int elf_fd = create_loader_elf_ex(&opts);
	ASSERT_GE(elf_fd, 0);

	struct test_ctx ctx = { .magic = 0xdeadbeef };
	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOEXEC);

	close(elf_fd);
	close(dev_fd);
}

TEST(missing_prog_section)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char map_data[] = "data";
	char license[] = "GPL";
	struct elf_builder_opts opts = {
		.insns = insns,
		.insns_sz = sizeof(insns),
		.map_data = map_data,
		.map_data_sz = sizeof(map_data),
		.license = license,
		.license_sz = sizeof(license),
		.omit_prog = true,
	};
	int elf_fd = create_loader_elf_ex(&opts);
	ASSERT_GE(elf_fd, 0);

	struct test_ctx ctx = { .magic = 0xdeadbeef };
	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOEXEC);

	close(elf_fd);
	close(dev_fd);
}

TEST(missing_map_section)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char map_data[] = "data";
	char license[] = "GPL";
	struct elf_builder_opts opts = {
		.insns = insns,
		.insns_sz = sizeof(insns),
		.map_data = map_data,
		.map_data_sz = sizeof(map_data),
		.license = license,
		.license_sz = sizeof(license),
		.omit_map = true,
	};
	int elf_fd = create_loader_elf_ex(&opts);
	ASSERT_GE(elf_fd, 0);

	struct test_ctx ctx = { .magic = 0xdeadbeef };
	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOEXEC);

	close(elf_fd);
	close(dev_fd);
}

TEST(missing_license_section)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char map_data[] = "data";
	char license[] = "GPL";
	struct elf_builder_opts opts = {
		.insns = insns,
		.insns_sz = sizeof(insns),
		.map_data = map_data,
		.map_data_sz = sizeof(map_data),
		.license = license,
		.license_sz = sizeof(license),
		.omit_license = true,
	};
	int elf_fd = create_loader_elf_ex(&opts);
	ASSERT_GE(elf_fd, 0);

	struct test_ctx ctx = { .magic = 0xdeadbeef };
	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOEXEC);

	close(elf_fd);
	close(dev_fd);
}

TEST(unaligned_prog_section)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	/* Pass 7 bytes instead of multiple of sizeof(struct bpf_insn) = 8 */
	char unaligned_insns[7] = { 0 };
	char map_data[] = "data";
	char license[] = "GPL";
	struct elf_builder_opts opts = {
		.insns = unaligned_insns,
		.insns_sz = sizeof(unaligned_insns),
		.map_data = map_data,
		.map_data_sz = sizeof(map_data),
		.license = license,
		.license_sz = sizeof(license),
	};
	int elf_fd = create_loader_elf_ex(&opts);
	ASSERT_GE(elf_fd, 0);

	struct test_ctx ctx = { .magic = 0xdeadbeef };
	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOEXEC);

	close(elf_fd);
	close(dev_fd);
}

TEST(empty_map_section)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char license[] = "GPL";
	struct elf_builder_opts opts = {
		.insns = insns,
		.insns_sz = sizeof(insns),
		.license = license,
		.license_sz = sizeof(license),
		.empty_map = true,
	};
	int elf_fd = create_loader_elf_ex(&opts);
	ASSERT_GE(elf_fd, 0);

	struct test_ctx ctx = { .magic = 0xdeadbeef };
	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOEXEC);

	close(elf_fd);
	close(dev_fd);
}

TEST(non_null_terminated_license)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char map_data[] = "data";
	char raw_license[] = { 'G', 'P', 'L' }; /* no \0 */
	struct elf_builder_opts opts = {
		.insns = insns,
		.insns_sz = sizeof(insns),
		.map_data = map_data,
		.map_data_sz = sizeof(map_data),
		.license = raw_license,
		.license_sz = sizeof(raw_license),
	};
	int elf_fd = create_loader_elf_ex(&opts);
	ASSERT_GE(elf_fd, 0);

	struct test_ctx ctx = { .magic = 0xdeadbeef };
	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOEXEC);

	close(elf_fd);
	close(dev_fd);
}

TEST(basic_load_and_run)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct bpf_insn insns[] = {
		BPF_ST_MEM(BPF_W, BPF_REG_1, offsetof(struct test_ctx, result), 0x12345678),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char map_data[] = "hello_loader";
	char license[] = "GPL";
	struct test_ctx ctx = { .magic = 0xdeadbeef, .result = 0 };

	int elf_fd = create_valid_loader_elf(insns, sizeof(insns),
					     map_data, sizeof(map_data),
					     license, sizeof(license));
	ASSERT_GE(elf_fd, 0);

	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(ctx.result, 0x12345678);

	close(elf_fd);
	close(dev_fd);
}

TEST(loader_failure)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, -EPERM),
		BPF_EXIT_INSN(),
	};
	char map_data[] = "dummy_map";
	char license[] = "GPL";
	struct test_ctx ctx = { .magic = 0xdeadbeef, .result = 0 };

	int elf_fd = create_valid_loader_elf(insns, sizeof(insns),
					     map_data, sizeof(map_data),
					     license, sizeof(license));
	ASSERT_GE(elf_fd, 0);

	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EPERM);

	close(elf_fd);
	close(dev_fd);
}

TEST(unprivileged_drop_caps)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	/*
	 * Drop CAP_BPF and CAP_SYS_ADMIN by either switching to an
	 * unprivileged UID or entering a new user namespace.
	 */
	if (unshare(CLONE_NEWUSER) != 0) {
		if (getuid() == 0) {
			if (setresuid(65534, 65534, 65534) != 0 &&
			    setresuid(1000, 1000, 1000) != 0) {
				close(dev_fd);
				SKIP(return, "Unable to switch to unprivileged UID or namespace");
			}
		} else {
			close(dev_fd);
			SKIP(return, "unshare(CLONE_NEWUSER) not permitted");
		}
	}

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	char map_data[] = "data";
	char license[] = "GPL";
	struct test_ctx ctx = { .magic = 0xdeadbeef };

	int elf_fd = create_valid_loader_elf(insns, sizeof(insns),
					     map_data, sizeof(map_data),
					     license, sizeof(license));
	ASSERT_GE(elf_fd, 0);

	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = sizeof(ctx),
		.ctx = (uintptr_t)&ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EPERM);

	close(elf_fd);
	close(dev_fd);
}

TEST(real_light_skeleton)
{
	int dev_fd = open_dev_or_skip(_metadata);
	if (dev_fd < 0)
		return;

	struct test_loader_lskel skel = { 0 };
	skel.ctx.sz = (char *)&skel.links - (char *)&skel;

	int elf_fd = create_valid_loader_elf(test_loader_opts_insn, test_loader_opts_insn_sz,
					     test_loader_opts_data, test_loader_opts_data_sz,
					     "GPL", 4);
	ASSERT_GE(elf_fd, 0);

	struct android_bpf_fd_loader_arg arg = {
		.loader_fd = elf_fd,
		.ctx_size = skel.ctx.sz,
		.ctx = (uintptr_t)&skel.ctx,
	};

	int ret = ioctl(dev_fd, ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD, &arg);
	ASSERT_EQ(ret, 0);

	EXPECT_TRUE(skel.maps.test_map.map_fd > 0);
	EXPECT_TRUE(skel.progs.probe.prog_fd > 0);

	/* Verify map metadata */
	struct bpf_map_info minfo = { 0 };
	union bpf_attr attr = { 0 };
	attr.info.bpf_fd = skel.maps.test_map.map_fd;
	attr.info.info_len = sizeof(minfo);
	attr.info.info = (uintptr_t)&minfo;
	ret = syscall(__NR_bpf, BPF_OBJ_GET_INFO_BY_FD, &attr, sizeof(attr));
	EXPECT_EQ(ret, 0);
	EXPECT_EQ(minfo.type, BPF_MAP_TYPE_ARRAY);
	EXPECT_EQ(minfo.key_size, 4U);
	EXPECT_EQ(minfo.value_size, 8U);
	EXPECT_EQ(minfo.max_entries, 4U);
	EXPECT_STREQ("test_map", minfo.name);

	/* Verify map operations (update and lookup) */
	uint32_t key = 0;
	uint64_t val = 0xdeadbeefcafebabeULL;
	uint64_t lookup_val = 0;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = skel.maps.test_map.map_fd;
	attr.key = (uintptr_t)&key;
	attr.value = (uintptr_t)&val;
	ret = syscall(__NR_bpf, BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
	EXPECT_EQ(ret, 0);

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = skel.maps.test_map.map_fd;
	attr.key = (uintptr_t)&key;
	attr.value = (uintptr_t)&lookup_val;
	ret = syscall(__NR_bpf, BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
	EXPECT_EQ(ret, 0);
	EXPECT_EQ(lookup_val, val);

	/* Verify prog metadata */
	struct bpf_prog_info pinfo = { 0 };
	memset(&attr, 0, sizeof(attr));
	attr.info.bpf_fd = skel.progs.probe.prog_fd;
	attr.info.info_len = sizeof(pinfo);
	attr.info.info = (uintptr_t)&pinfo;
	ret = syscall(__NR_bpf, BPF_OBJ_GET_INFO_BY_FD, &attr, sizeof(attr));
	EXPECT_EQ(ret, 0);
	EXPECT_EQ(pinfo.type, BPF_PROG_TYPE_SOCKET_FILTER);
	EXPECT_STREQ("probe", pinfo.name);

	if (skel.progs.probe.prog_fd > 0)
		close(skel.progs.probe.prog_fd);
	if (skel.maps.test_map.map_fd > 0)
		close(skel.maps.test_map.map_fd);
	close(elf_fd);
	close(dev_fd);
}

TEST_HARNESS_MAIN
