// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

/* Check that pagemap's exclusive bit is evaluated per PPPS file slice. */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/personality.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef ADDR_4KB_COMPAT_PAGE_SIZE
#define ADDR_4KB_COMPAT_PAGE_SIZE 0x40000000
#endif

#define USER_PAGE_SIZE 4096UL
#define NR_SLICES 4
#define MAPPING_SIZE (NR_SLICES * USER_PAGE_SIZE)
#define BIT_ULL(nr) (1ULL << (nr))
#define PM_MMAP_EXCLUSIVE BIT_ULL(56)
#define PM_FILE BIT_ULL(61)
#define PM_PRESENT BIT_ULL(63)

static int failures;
static int test_no;

static void result(bool pass, const char *name)
{
	printf("%s %d - %s\n", pass ? "ok" : "not ok", ++test_no, name);
	if (!pass)
		failures++;
}

static int read_pagemap(void *address, uint64_t *entry)
{
	off_t offset = ((uintptr_t)address / USER_PAGE_SIZE) * sizeof(*entry);
	int fd;
	ssize_t n;

	fd = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	n = pread(fd, entry, sizeof(*entry), offset);
	close(fd);
	if (n != sizeof(*entry)) {
		if (n >= 0)
			errno = EIO;
		return -1;
	}
	return 0;
}

static bool check_exclusive(unsigned char *mapping, unsigned int exclusive_mask)
{
	unsigned int observed = 0;
	unsigned int i;

	for (i = 0; i < NR_SLICES; i++) {
		uint64_t entry;

		if (read_pagemap(mapping + i * USER_PAGE_SIZE, &entry)) {
			perror("read pagemap");
			return false;
		}
		if ((entry & (PM_PRESENT | PM_FILE)) != (PM_PRESENT | PM_FILE)) {
			printf("# slice %u missing present/file flags: %#llx\n", i,
			       (unsigned long long)entry);
			return false;
		}
		if (entry & PM_MMAP_EXCLUSIVE)
			observed |= 1U << i;
	}
	printf("# exclusive mask: expected=%#x observed=%#x\n",
	       exclusive_mask, observed);
	return observed == exclusive_mask;
}

static int run_test(void)
{
	unsigned char *mapping = MAP_FAILED;
	unsigned char *alias = MAP_FAILED;
	unsigned int i;
	uint64_t alias_entry;
	int fd = -1;

	printf("TAP version 13\n1..5\n");
	result(sysconf(_SC_PAGESIZE) == USER_PAGE_SIZE,
	       "process page size is 4K");
	if (failures)
		goto out;

	fd = syscall(SYS_memfd_create, "pagemap-exclusive-ppps", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, MAPPING_SIZE)) {
		perror("prepare memfd");
		goto out;
	}
	mapping = mmap(NULL, MAPPING_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		       fd, 0);
	if (mapping == MAP_FAILED) {
		perror("mmap target");
		goto out;
	}
	for (i = 0; i < NR_SLICES; i++)
		mapping[i * USER_PAGE_SIZE] = (unsigned char)i;
	result(check_exclusive(mapping, 0xf),
	       "disjoint file slices are exclusively mapped");

	alias = mmap(NULL, USER_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		     fd, 0);
	if (alias == MAP_FAILED) {
		perror("mmap alias");
		goto out;
	}
	alias[0] ^= 1;
	result(check_exclusive(mapping, 0xe),
	       "only the aliased slice loses exclusivity");

	if (read_pagemap(alias, &alias_entry)) {
		perror("read alias pagemap");
		goto out;
	}
	result(!(alias_entry & PM_MMAP_EXCLUSIVE),
	       "the second mapping of a slice is not exclusive");

	munmap(alias, USER_PAGE_SIZE);
	alias = MAP_FAILED;
	result(check_exclusive(mapping, 0xf),
	       "unmapping the alias restores exclusivity");

out:
	if (test_no < 5)
		failures += 5 - test_no;
	if (alias != MAP_FAILED)
		munmap(alias, USER_PAGE_SIZE);
	if (mapping != MAP_FAILED)
		munmap(mapping, MAPPING_SIZE);
	if (fd >= 0)
		close(fd);
	printf("# Totals: pass:%d fail:%d\n", 5 - failures, failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	int persona;

	if (access("/proc/self", F_OK) &&
	    mount("proc", "/proc", "proc", 0, NULL)) {
		perror("mount proc");
		return EXIT_FAILURE;
	}
	if (argc == 2 && !strcmp(argv[1], "--run"))
		return run_test();

	persona = personality(0xffffffffUL);
	if (persona < 0 ||
	    personality(persona | ADDR_4KB_COMPAT_PAGE_SIZE) < 0) {
		perror("personality");
		return EXIT_FAILURE;
	}
	execl("/proc/self/exe", argv[0], "--run", NULL);
	perror("exec");
	return EXIT_FAILURE;
}
