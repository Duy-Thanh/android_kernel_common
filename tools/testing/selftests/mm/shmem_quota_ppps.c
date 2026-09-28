// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <unistd.h>

#define NATIVE_PAGE_SIZE 16384
#define KSFT_SKIP 4

static void die(const char *what)
{
	perror(what);
	exit(EXIT_FAILURE);
}

static int uffd_copy(int uffd, void *dst, void *src, size_t len)
{
	struct uffdio_copy copy = {
		.dst = (uintptr_t)dst,
		.src = (uintptr_t)src,
		.len = len,
	};

	return ioctl(uffd, UFFDIO_COPY, &copy) == 0 &&
	       copy.copy == (long long)len;
}

int main(void)
{
	struct uffdio_register reg = { .mode = UFFDIO_REGISTER_MODE_MISSING };
	struct uffdio_api api = { .api = UFFD_API };
	char dir[64], path[80];
	void *dst, *src;
	long page_size = sysconf(_SC_PAGESIZE);
	int fd, uffd, ret = EXIT_FAILURE;

	if (page_size != 4096)
		return KSFT_SKIP;
	if (geteuid())
		return KSFT_SKIP;

	snprintf(dir, sizeof(dir), "/tmp/shmem-quota-ppps-%ld", (long)getpid());
	snprintf(path, sizeof(path), "%s/data", dir);
	if (mkdir(dir, 0700) ||
	    mount("tmpfs", dir, "tmpfs", 0,
		  "size=16384,nr_inodes=16,huge=never"))
		die("private tmpfs");

	fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (fd < 0 || ftruncate(fd, NATIVE_PAGE_SIZE))
		die("file");
	dst = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		   MAP_SHARED, fd, 0);
	src = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (dst == MAP_FAILED || src == MAP_FAILED)
		die("mmap");
	memset(src, 0x5a, NATIVE_PAGE_SIZE);

	uffd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK |
			   UFFD_USER_MODE_ONLY);
	if (uffd < 0 && (errno == ENOSYS || errno == EPERM)) {
		ret = KSFT_SKIP;
		goto out;
	}
	if (uffd < 0 || ioctl(uffd, UFFDIO_API, &api))
		die("userfaultfd");
	reg.range.start = (uintptr_t)dst;
	reg.range.len = NATIVE_PAGE_SIZE;
	if (ioctl(uffd, UFFDIO_REGISTER, &reg))
		die("UFFDIO_REGISTER");

	if (!uffd_copy(uffd, dst, src, page_size))
		die("first UFFDIO_COPY");
	/* The backing folio already exists and tmpfs has no free block. */
	if (!uffd_copy(uffd, (char *)dst + page_size,
		       (char *)src + page_size, page_size)) {
		fprintf(stderr, "sibling UFFDIO_COPY failed at quota: %s\n",
			strerror(errno));
		goto out_close;
	}
	if (memcmp(dst, src, 2 * page_size)) {
		fprintf(stderr, "filled slices differ\n");
		goto out_close;
	}
	ret = EXIT_SUCCESS;

out_close:
	close(uffd);
out:
	munmap(dst, NATIVE_PAGE_SIZE);
	munmap(src, NATIVE_PAGE_SIZE);
	close(fd);
	unlink(path);
	if (umount(dir))
		die("umount");
	rmdir(dir);
	return ret;
}
