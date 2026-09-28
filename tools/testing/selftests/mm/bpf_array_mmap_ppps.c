// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process maps a BPF_F_MMAPABLE array in 4K value slices and is
 * refused mappings that extend past the array or into its native-page padding.
 */
#define _GNU_SOURCE

#include <linux/bpf.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define MAP_ENTRIES	4
#define MAP_SIZE	(MAP_ENTRIES * PROCESS_PAGE_SIZE)
#define SMALL_VALUE_SIZE	64

static int sys_bpf(enum bpf_cmd command, union bpf_attr *attr)
{
	return syscall(__NR_bpf, command, attr, sizeof(*attr));
}

static int create_array(void)
{
	union bpf_attr attr = {
		.map_type = BPF_MAP_TYPE_ARRAY,
		.key_size = sizeof(unsigned int),
		.value_size = PROCESS_PAGE_SIZE,
		.max_entries = MAP_ENTRIES,
		.map_flags = BPF_F_MMAPABLE,
	};

	return sys_bpf(BPF_MAP_CREATE, &attr);
}

static int create_small_array(void)
{
	union bpf_attr attr = {
		.map_type = BPF_MAP_TYPE_ARRAY,
		.key_size = sizeof(unsigned int),
		.value_size = SMALL_VALUE_SIZE,
		.max_entries = 1,
		.map_flags = BPF_F_MMAPABLE,
	};

	return sys_bpf(BPF_MAP_CREATE, &attr);
}

static bool initialize_array(int map_fd)
{
	unsigned char *value = malloc(PROCESS_PAGE_SIZE);
	unsigned int key;
	bool success = true;

	if (!value)
		return false;
	for (key = 0; key < MAP_ENTRIES; key++) {
		union bpf_attr attr = {
			.map_fd = map_fd,
			.key = (unsigned long)&key,
			.value = (unsigned long)value,
			.flags = BPF_ANY,
		};

		memset(value, 0x41 + key, PROCESS_PAGE_SIZE);
		if (sys_bpf(BPF_MAP_UPDATE_ELEM, &attr)) {
			success = false;
			break;
		}
	}
	free(value);
	return success;
}

static bool mapped_values_match(const unsigned char *mapping)
{
	unsigned int key;

	for (key = 0; key < MAP_ENTRIES; key++) {
		pid_t child = fork();
		int status;

		if (child < 0)
			return false;
		if (!child)
			_exit(mapping[key * PROCESS_PAGE_SIZE] == 0x41 + key ? 0 : 1);
		if (waitpid(child, &status, 0) != child ||
		    !WIFEXITED(status) || WEXITSTATUS(status))
			return false;
	}
	return true;
}

static int run_test(void)
{
	struct rlimit memlock = {
		.rlim_cur = RLIM_INFINITY,
		.rlim_max = RLIM_INFINITY,
	};
	unsigned char *mapping;
	void *small_mapping;
	void *small_padding;
	void *past_end;
	bool initialized;
	bool bounds_ok;
	bool values_ok;
	int map_fd;
	int small_map_fd;

	ksft_print_header();
	ksft_set_plan(6);
	setrlimit(RLIMIT_MEMLOCK, &memlock);

	map_fd = create_array();
	if (map_fd < 0) {
		if (errno == EPERM || errno == EACCES)
			ksft_exit_skip("BPF map creation is unavailable: %s\n",
				       strerror(errno));
		ksft_exit_fail_msg("BPF_MAP_CREATE failed: %s\n",
				   strerror(errno));
	}
	ksft_test_result(true, "create a 16K mmapable BPF array\n");
	initialized = initialize_array(map_fd);
	ksft_test_result(initialized, "initialize all four 4K array values\n");
	if (!initialized)
		ksft_exit_fail_msg("BPF_MAP_UPDATE_ELEM failed: %s\n",
				   strerror(errno));

	mapping = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		       map_fd, 0);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("valid BPF array mmap failed: %s\n",
				   strerror(errno));
	values_ok = mapped_values_match(mapping);
	ksft_test_result(values_ok,
			 "every 4K slice maps the corresponding array value\n");

	errno = 0;
	past_end = mmap(NULL, 2 * PROCESS_PAGE_SIZE, PROT_READ, MAP_SHARED,
			map_fd, 3 * PROCESS_PAGE_SIZE);
	bounds_ok = past_end == MAP_FAILED && errno == EINVAL;
	ksft_test_result(bounds_ok, "reject an mmap extending past the array\n");
	ksft_print_msg("out-of-bounds mmap=%p errno=%d\n", past_end, errno);

	small_map_fd = create_small_array();
	if (small_map_fd < 0)
		ksft_exit_fail_msg("small BPF_MAP_CREATE failed: %s\n",
				   strerror(errno));
	small_mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
			     MAP_SHARED, small_map_fd, 0);
	ksft_test_result(small_mapping != MAP_FAILED,
			 "map one process page for a small BPF array\n");

	errno = 0;
	small_padding = mmap(NULL, 2 * PROCESS_PAGE_SIZE, PROT_READ,
			     MAP_SHARED, small_map_fd, 0);
	bounds_ok = small_padding == MAP_FAILED && errno == EINVAL;
	ksft_test_result(bounds_ok,
			 "reject mapping native-page padding of a small array\n");
	ksft_print_msg("small-array padding mmap=%p errno=%d\n",
		       small_padding, errno);

	if (past_end != MAP_FAILED)
		munmap(past_end, 2 * PROCESS_PAGE_SIZE);
	if (small_padding != MAP_FAILED)
		munmap(small_padding, 2 * PROCESS_PAGE_SIZE);
	if (small_mapping != MAP_FAILED)
		munmap(small_mapping, PROCESS_PAGE_SIZE);
	close(small_map_fd);
	munmap(mapping, MAP_SIZE);
	close(map_fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
