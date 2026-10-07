// SPDX-License-Identifier: GPL-2.0
/*
 * ptrace and /proc/<pid>/mem reach VM_IO|VM_PFNMAP mappings through
 * vm_ops->access, which for PCI sysfs resources is generic_access_phys().
 * It must read the same device bytes as a load through the mapping, in a
 * 4K compat process just like in a native one.  Deriving the physical
 * address from a native 16K PFN shifted by the 4K process page shift reads
 * an unrelated physical address instead.
 *
 * The test needs a stable, nonzero memory BAR on which native loads and
 * /proc/self/mem agree.  Virtio register BARs on Cuttlefish do not provide
 * that oracle: the mmap load can differ from generic_access_phys() even in
 * a native process.  Probe candidates first, then use the same BAR for both
 * geometries; skip if the host exposes no usable BAR.
 */
#define _GNU_SOURCE

#include <dirent.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define IORESOURCE_MEM	0x00000200
#define COMPARE_BYTES	32

static bool find_bar(unsigned int wanted, char path[PATH_MAX])
{
	DIR *dir = opendir("/sys/bus/pci/devices");
	struct dirent *entry;
	bool found = false;
	unsigned int seen = 0;

	if (!dir)
		return false;
	while (!found && (entry = readdir(dir))) {
		unsigned long long start, end, flags;
		char resource[PATH_MAX];
		unsigned int bar = 0;
		FILE *file;

		if (entry->d_name[0] == '.')
			continue;
		snprintf(resource, sizeof(resource),
			 "/sys/bus/pci/devices/%s/resource", entry->d_name);
		file = fopen(resource, "re");
		if (!file)
			continue;
		while (fscanf(file, "%llx %llx %llx", &start, &end, &flags) == 3) {
			if ((flags & IORESOURCE_MEM) && end > start &&
			    end - start + 1 >= NATIVE_PAGE_SIZE) {
				snprintf(path, PATH_MAX,
					 "/sys/bus/pci/devices/%s/resource%u",
					 entry->d_name, bar);
				if (!access(path, R_OK) && seen++ == wanted) {
					found = true;
					break;
				}
			}
			bar++;
		}
		fclose(file);
	}
	closedir(dir);
	return found;
}

static int run_case(const char *path, bool verbose)
{
	uint32_t direct[COMPARE_BYTES / 4], via_mem[COMPARE_BYTES / 4];
	volatile uint32_t *map;
	unsigned int i;
	int fd, mem;
	ssize_t got;
	bool differ = false;
	bool nonzero = false;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		if (verbose)
			ksft_print_msg("%s: %s\n", path, strerror(errno));
		return KSFT_SKIP;
	}
	map = mmap(NULL, getpagesize(), PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		if (verbose)
			ksft_print_msg("mmap %s: %s\n", path, strerror(errno));
		close(fd);
		return KSFT_FAIL;
	}
	for (i = 0; i < COMPARE_BYTES / 4; i++) {
		direct[i] = map[i];
		nonzero |= direct[i] != 0;
	}
	mem = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
	if (mem < 0) {
		munmap((void *)map, getpagesize());
		close(fd);
		return KSFT_FAIL;
	}
	got = pread(mem, via_mem, sizeof(via_mem), (off_t)(uintptr_t)map);
	if (got != sizeof(via_mem)) {
		if (verbose)
			ksft_print_msg("/proc/self/mem read of %s returned %zd (%s)\n",
				       path, got, got < 0 ? strerror(errno) : "short");
		munmap((void *)map, getpagesize());
		close(mem);
		close(fd);
		return KSFT_FAIL;
	}
	for (i = 0; i < COMPARE_BYTES / 4; i++)
		if (direct[i] != via_mem[i])
			differ = true;
	if (verbose)
		ksft_print_msg("%s page %d: %s%s\n", path, getpagesize(),
			       differ ? "MISMATCH" : "match",
			       nonzero ? "" : " (all zero)");
	if (verbose && differ)
		for (i = 0; i < COMPARE_BYTES / 4; i++)
			ksft_print_msg("  +%02u: load %08x  /proc/self/mem %08x\n",
				       i * 4, direct[i], via_mem[i]);
	munmap((void *)map, getpagesize());
	close(mem);
	close(fd);
	return differ ? KSFT_FAIL : nonzero ? KSFT_PASS : KSFT_SKIP;
}

static int run_child(bool compat, const char *mode, const char *path)
{
	int status;
	pid_t pid = fork();

	if (pid < 0)
		ksft_exit_fail_msg("fork: %s\n", strerror(errno));
	if (!pid) {
		ppps_execl(compat, NULL, mode, path, NULL);
		_exit(KSFT_SKIP);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return KSFT_FAIL;
	return WEXITSTATUS(status);
}

static void report(int ret, const char *what)
{
	if (ret == KSFT_SKIP)
		ksft_test_result_skip("%s\n", what);
	else
		ksft_test_result(ret == KSFT_PASS, "%s\n", what);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);
	char path[PATH_MAX];
	unsigned int candidate;
	bool selected = false;

	if (mode && argc == 3 && !strcmp(mode, "--native"))
		return getpagesize() == (int)NATIVE_PAGE_SIZE ?
			run_case(argv[2], true) : KSFT_SKIP;
	if (mode && argc == 3 && !strcmp(mode, "--compat"))
		return ppps_is_compat_process() ?
			run_case(argv[2], true) : KSFT_SKIP;
	if (mode && argc == 3 && !strcmp(mode, "--probe"))
		return getpagesize() == (int)NATIVE_PAGE_SIZE ?
			run_case(argv[2], false) : KSFT_SKIP;

	ksft_print_header();
	if (geteuid())
		ksft_exit_skip("root is required to map PCI resources\n");
	ksft_set_plan(2);
	fflush(stdout);
	for (candidate = 0; find_bar(candidate, path); candidate++)
		if (run_child(false, "--probe", path) == KSFT_PASS) {
			selected = true;
			break;
		}
	if (!selected) {
		ksft_print_msg("no stable nonzero PCI BAR: native control unavailable\n");
		ksft_test_result_skip("native PCI BAR access oracle unavailable\n");
		ksft_test_result_skip("4K compat PCI BAR access oracle unavailable\n");
		ksft_finished();
	}
	report(run_child(false, "--native", path),
	       "native /proc/self/mem read of a PCI BAR matches a load");
	report(run_child(true, "--compat", path),
	       "4K compat /proc/self/mem read of the same PCI BAR matches a load");
	ksft_finished();
}
