// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process maps the QEMU EDU PCI BAR through sysfs resource0 in
 * process-page units, and a 4K-offset slice mapping lands on the matching
 * BAR offset.
 */
#define _GNU_SOURCE

#include <dirent.h>
#include <limits.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"

#define EDU_VENDOR 0x1234
#define EDU_DEVICE 0x11e8

static int read_hex_value(const char *path, unsigned int *value)
{
	FILE *file = fopen(path, "re");
	int matched;

	if (!file)
		return -errno;
	matched = fscanf(file, "%x", value);
	fclose(file);
	return matched == 1 ? 0 : -EINVAL;
}

static int find_edu_resource(char resource_path[PATH_MAX])
{
	const char *devices_path = "/sys/bus/pci/devices";
	char value_path[PATH_MAX];
	struct dirent *entry;
	unsigned int device;
	unsigned int vendor;
	DIR *devices;

	devices = opendir(devices_path);
	if (!devices)
		return -errno;
	while ((entry = readdir(devices))) {
		if (entry->d_name[0] == '.')
			continue;
		snprintf(value_path, sizeof(value_path), "%s/%s/vendor",
			 devices_path, entry->d_name);
		if (read_hex_value(value_path, &vendor))
			continue;
		snprintf(value_path, sizeof(value_path), "%s/%s/device",
			 devices_path, entry->d_name);
		if (read_hex_value(value_path, &device))
			continue;
		if (vendor != EDU_VENDOR || device != EDU_DEVICE)
			continue;
		snprintf(resource_path, PATH_MAX, "%s/%s/resource0",
			 devices_path, entry->d_name);
		closedir(devices);
		return 0;
	}
	closedir(devices);
	return -ENOENT;
}

static int resource_size(const char *resource_path, size_t *size)
{
	unsigned long long flags;
	unsigned long long start;
	unsigned long long end;
	char path[PATH_MAX];
	char *slash;
	FILE *file;

	if (snprintf(path, sizeof(path), "%s", resource_path) >=
	    (int)sizeof(path))
		return -ENAMETOOLONG;
	slash = strrchr(path, '/');
	if (!slash)
		return -EINVAL;
	strcpy(slash + 1, "resource");
	file = fopen(path, "re");
	if (!file)
		return -errno;
	if (fscanf(file, "%llx %llx %llx", &start, &end, &flags) != 3) {
		fclose(file);
		return -EINVAL;
	}
	fclose(file);
	if (end < start || end - start + 1 > SIZE_MAX)
		return -EOVERFLOW;
	*size = end - start + 1;
	return 0;
}

static int run_test(void)
{
	char resource_path[PATH_MAX];
	uint32_t *full_words;
	uint32_t *slice_words;
	void *slice_mapping;
	void *mapping;
	size_t size = 0;
	int error;
	int fd;

	ksft_print_header();
	error = find_edu_resource(resource_path);
	if (error)
		ksft_exit_skip("QEMU EDU PCI device is unavailable\n");
	error = resource_size(resource_path, &size);
	if (error)
		ksft_exit_fail_msg("cannot read EDU BAR size\n");

	ksft_set_plan(3);
	ksft_test_result(size > PROCESS_PAGE_SIZE && !(size % PROCESS_PAGE_SIZE),
			 "EDU BAR spans multiple process pages\n");

	fd = ppps_open_fixture_or_skip(resource_path, O_RDWR | O_SYNC);
	mapping = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	ksft_test_result(mapping != MAP_FAILED,
			 "map the complete PCI BAR in process page units\n");
	if (mapping == MAP_FAILED) {
		ksft_test_result_skip("complete BAR mapping is unavailable\n");
		close(fd);
		ksft_finished();
	}

	slice_mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
			     MAP_SHARED, fd, PROCESS_PAGE_SIZE);
	full_words = mapping;
	slice_words = slice_mapping;
	ksft_test_result(slice_mapping != MAP_FAILED &&
			 slice_words[0] == full_words[PROCESS_PAGE_SIZE / sizeof(*full_words)],
			 "preserve a process-page PCI BAR offset\n");
	if (slice_mapping != MAP_FAILED)
		munmap(slice_mapping, PROCESS_PAGE_SIZE);
	munmap(mapping, size);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
