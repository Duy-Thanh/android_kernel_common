// SPDX-License-Identifier: GPL-2.0
/* Regression test for AArch32 mmap2 offsets on arm64 PPPS kernels. */

#define __NR_exit 1
#define __NR_read 3
#define __NR_write 4
#define __NR_open 5
#define __NR_close 6
#define __NR_lseek 19
#define __NR_munmap 91
#define __NR_mmap2 192

#define O_RDONLY 0
#define PROT_READ 1
#define MAP_PRIVATE 2
#define PAGE_SIZE_4K 4096

static const char busybox_path[] = "/bin/busybox";

static inline long
syscall1(long number, long arg1)
{
	register long r0 __asm__("r0") = arg1;
	register long r7 __asm__("r7") = number;

	__asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory", "cc");
	return r0;
}

static inline long
syscall3(long number, long arg1, long arg2, long arg3)
{
	register long r0 __asm__("r0") = arg1;
	register long r1 __asm__("r1") = arg2;
	register long r2 __asm__("r2") = arg3;
	register long r7 __asm__("r7") = number;

	__asm__ volatile("svc #0"
			 : "+r"(r0)
			 : "r"(r1), "r"(r2), "r"(r7)
			 : "memory", "cc");
	return r0;
}

static inline long
syscall6(long number, long arg1, long arg2, long arg3, long arg4, long arg5,
	 long arg6)
{
	register long r0 __asm__("r0") = arg1;
	register long r1 __asm__("r1") = arg2;
	register long r2 __asm__("r2") = arg3;
	register long r3 __asm__("r3") = arg4;
	register long r4 __asm__("r4") = arg5;
	register long r5 __asm__("r5") = arg6;
	register long r7 __asm__("r7") = number;

	__asm__ volatile("svc #0"
			 : "+r"(r0)
			 : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5),
			   "r"(r7)
			 : "memory", "cc");
	return r0;
}

static unsigned long string_length(const char *text)
{
	unsigned long length = 0;

	while (text[length])
		length++;
	return length;
}

static void print(const char *text)
{
	syscall3(__NR_write, 1, (long)text, string_length(text));
}

static int bytes_differ(const unsigned char *left, const unsigned char *right,
			unsigned long length)
{
	unsigned long i;

	for (i = 0; i < length; i++) {
		if (left[i] != right[i])
			return 1;
	}
	return 0;
}

static int test_offset(int fd, unsigned long pgoff)
{
	unsigned char expected[64];
	unsigned char *mapping;
	long ret;

	ret = syscall3(__NR_lseek, fd, pgoff * PAGE_SIZE_4K, 0);
	if (ret < 0)
		return 0;
	ret = syscall3(__NR_read, fd, (long)expected, sizeof(expected));
	if (ret != sizeof(expected))
		return 0;

	mapping = (void *)syscall6(__NR_mmap2, 0, PAGE_SIZE_4K, PROT_READ,
				   MAP_PRIVATE, fd, pgoff);
	if ((unsigned long)mapping >= (unsigned long)-4095)
		return 0;
	ret = !bytes_differ(mapping, expected, sizeof(expected));
	syscall3(__NR_munmap, (long)mapping, PAGE_SIZE_4K, 0);
	return ret;
}

void _start(void)
{
	long fd;
	int failures = 0;

	print("TAP version 13\n1..4\n");
	fd = syscall3(__NR_open, (long)busybox_path, O_RDONLY, 0);
	if (fd < 0) {
		print("not ok 1 - open file backing for mmap2\n");
		print("not ok 2 - map file at 4K offset\n");
		print("not ok 3 - map file at 8K offset\n");
		print("not ok 4 - map file at 12K offset\n");
		syscall1(__NR_exit, 1);
	}

	print("ok 1 - open file backing for mmap2\n");
	if (test_offset(fd, 1)) {
		print("ok 2 - map file at 4K offset\n");
	} else {
		print("not ok 2 - map file at 4K offset\n");
		failures++;
	}
	if (test_offset(fd, 2)) {
		print("ok 3 - map file at 8K offset\n");
	} else {
		print("not ok 3 - map file at 8K offset\n");
		failures++;
	}
	if (test_offset(fd, 3)) {
		print("ok 4 - map file at 12K offset\n");
	} else {
		print("not ok 4 - map file at 12K offset\n");
		failures++;
	}

	syscall1(__NR_close, fd);
	syscall1(__NR_exit, failures != 0);
	__builtin_unreachable();
}
