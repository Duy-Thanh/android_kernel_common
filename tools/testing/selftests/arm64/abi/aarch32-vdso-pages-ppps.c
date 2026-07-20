// SPDX-License-Identifier: GPL-2.0
/* Regression test for AArch32 ABI page mappings on arm64 PPPS kernels. */

#define __NR_exit 1
#define __NR_read 3
#define __NR_write 4
#define __NR_open 5
#define __NR_close 6

#define O_RDONLY 0
#define PAGE_SIZE_4K 4096
#define MAPS_CAPACITY 16384

static const char maps_path[] = "/proc/self/maps";
static char maps[MAPS_CAPACITY];

static inline long syscall1(long number, long arg1)
{
	register long r0 __asm__("r0") = arg1;
	register long r7 __asm__("r7") = number;

	__asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory", "cc");
	return r0;
}

static inline long syscall3(long number, long arg1, long arg2, long arg3)
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

static int matches(unsigned long offset, unsigned long length, const char *name)
{
	unsigned long i;

	for (i = 0; name[i]; i++) {
		if (offset + i >= length || maps[offset + i] != name[i])
			return 0;
	}
	return 1;
}

static int hex_value(char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	return -1;
}

static unsigned long parse_hex(unsigned long *offset, unsigned long length)
{
	unsigned long value = 0;
	int digit;

	while (*offset < length) {
		digit = hex_value(maps[*offset]);
		if (digit < 0)
			break;
		value = (value << 4) | digit;
		(*offset)++;
	}
	return value;
}

static long mapping_size(const char *name, unsigned long length)
{
	unsigned long end, line, offset, start;

	for (offset = 0; offset < length; offset++) {
		if (!matches(offset, length, name))
			continue;
		line = offset;
		while (line && maps[line - 1] != '\n')
			line--;
		start = parse_hex(&line, length);
		if (line >= length || maps[line++] != '-')
			return -1;
		end = parse_hex(&line, length);
		if (end <= start)
			return -1;
		return end - start;
	}
	return -1;
}

void _start(void)
{
	long fd, length;
	int failures = 0;

	print("TAP version 13\n1..3\n");
	fd = syscall3(__NR_open, (long)maps_path, O_RDONLY, 0);
	length = fd < 0 ? -1 : syscall3(__NR_read, fd, (long)maps,
					       sizeof(maps));
	if (fd >= 0)
		syscall1(__NR_close, fd);
	if (length <= 0) {
		print("not ok 1 - read AArch32 process maps\n");
		print("not ok 2 - vectors mapping uses one process page\n");
		print("not ok 3 - sigpage mapping uses one process page\n");
		syscall1(__NR_exit, 1);
	}

	print("ok 1 - read AArch32 process maps\n");
	if (mapping_size("[vectors]", length) == PAGE_SIZE_4K) {
		print("ok 2 - vectors mapping uses one process page\n");
	} else {
		print("not ok 2 - vectors mapping uses one process page\n");
		failures++;
	}
	if (mapping_size("[sigpage]", length) == PAGE_SIZE_4K) {
		print("ok 3 - sigpage mapping uses one process page\n");
	} else {
		print("not ok 3 - sigpage mapping uses one process page\n");
		failures++;
	}

	syscall1(__NR_exit, failures != 0);
	__builtin_unreachable();
}
