// SPDX-License-Identifier: GPL-2.0
/*
 * The four 4K slices of a 16K window of a 4K compat process share one
 * order-0 anonymous folio.  When the window is later split between several
 * VMAs (mprotect, munmap, PR_SET_VMA_ANON_NAME, ...) reverse mapping must
 * still find every slice: otherwise reclaim and migration can never unmap
 * the folio and it stays resident forever.
 *
 * The test runs in its own memory cgroup and drives reclaim through
 * memory.reclaim.  A window that was never split is the control: once it
 * has gone to swap, every split window must have gone to swap as well.
 * Root, cgroup v2 memory.reclaim and active swap are required.
 */
#define _GNU_SOURCE

#include <limits.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>

#include "ppps_tuple_test.h"

#ifndef PR_SET_VMA
#define PR_SET_VMA		0x53564d41
#define PR_SET_VMA_ANON_NAME	0
#endif

#define CGROUP_ROOT	"/sys/fs/cgroup"
#define RECLAIM_ROUNDS	40
#define FIRST_BYTE	0x31

enum split_kind { SPLIT_NONE, SPLIT_MPROTECT, SPLIT_MUNMAP, SPLIT_ANON_NAME };

struct window {
	const char *name;
	enum split_kind kind;
	unsigned char *reservation, *base;
	bool slice_mapped[PPPS_SLICES];
	bool usable;
};

static struct window windows[] = {
	{ "control (not split)", SPLIT_NONE },
	{ "split by mprotect", SPLIT_MPROTECT },
	{ "split by munmap", SPLIT_MUNMAP },
	{ "split by PR_SET_VMA_ANON_NAME", SPLIT_ANON_NAME },
};

static char cgroup_path[PATH_MAX];
static char original_cgroup[PATH_MAX];

static bool write_file(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	ssize_t len = strlen(value), ret;

	if (fd < 0)
		return false;
	ret = write(fd, value, len);
	close(fd);
	return ret == len;
}

static void leave_cgroup(void)
{
	char path[PATH_MAX + 32], pid[32];

	if (!cgroup_path[0])
		return;
	snprintf(pid, sizeof(pid), "%d", (int)getpid());
	snprintf(path, sizeof(path), "%s%s/cgroup.procs", CGROUP_ROOT,
		 original_cgroup);
	if (!write_file(path, pid))
		write_file(CGROUP_ROOT "/cgroup.procs", pid);
	rmdir(cgroup_path);
	cgroup_path[0] = '\0';
}

static bool enter_cgroup(void)
{
	char path[PATH_MAX + 32], pid[32], line[PATH_MAX + 8];
	FILE *self = fopen("/proc/self/cgroup", "re");

	if (!self)
		return false;
	while (fgets(line, sizeof(line), self)) {
		if (!strncmp(line, "0::", 3)) {
			line[strcspn(line, "\n")] = '\0';
			snprintf(original_cgroup, sizeof(original_cgroup), "%s",
				 line + 3);
		}
	}
	fclose(self);
	if (!strcmp(original_cgroup, "/"))
		original_cgroup[0] = '\0';
	snprintf(cgroup_path, sizeof(cgroup_path), CGROUP_ROOT "/ppps_rmap_%d",
		 (int)getpid());
	if (mkdir(cgroup_path, 0755)) {
		cgroup_path[0] = '\0';
		return false;
	}
	snprintf(pid, sizeof(pid), "%d", (int)getpid());
	snprintf(path, sizeof(path), "%s/cgroup.procs", cgroup_path);
	if (!write_file(path, pid)) {
		rmdir(cgroup_path);
		cgroup_path[0] = '\0';
		return false;
	}
	return true;
}

static void reclaim_round(void)
{
	char path[PATH_MAX + 32];

	snprintf(path, sizeof(path), "%s/memory.reclaim", cgroup_path);
	/* -EAGAIN just means less than asked was reclaimed. */
	write_file(path, "256M");
}

static bool setup_window(struct window *w)
{
	unsigned char *p;
	unsigned int i;

	w->base = map_aligned(NATIVE_PAGE_SIZE, &w->reservation);
	if (w->base == MAP_FAILED)
		return false;
	fill_tuple(w->base, FIRST_BYTE);
	for (i = 0; i < PPPS_SLICES; i++)
		w->slice_mapped[i] = true;
	p = w->base + 2 * PROCESS_PAGE_SIZE;
	switch (w->kind) {
	case SPLIT_NONE:
		break;
	case SPLIT_MPROTECT:
		if (mprotect(p, PROCESS_PAGE_SIZE, PROT_READ))
			return false;
		break;
	case SPLIT_MUNMAP:
		if (munmap(p, PROCESS_PAGE_SIZE))
			return false;
		w->slice_mapped[2] = false;
		break;
	case SPLIT_ANON_NAME:
		if (prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, (unsigned long)p,
			  PROCESS_PAGE_SIZE, (unsigned long)"ppps_split"))
			return false;
		break;
	}
	return true;
}

/* Number of mapped slices that are still resident. */
static unsigned int resident_slices(const struct window *w)
{
	unsigned int i, nr = 0;

	for (i = 0; i < PPPS_SLICES; i++)
		if (w->slice_mapped[i] &&
		    ppps_page_present(w->base + i * PROCESS_PAGE_SIZE))
			nr++;
	return nr;
}

static bool contents_intact(const struct window *w)
{
	unsigned int i;

	for (i = 0; i < PPPS_SLICES; i++)
		if (w->slice_mapped[i] &&
		    !all_bytes_are(w->base + i * PROCESS_PAGE_SIZE,
				   PROCESS_PAGE_SIZE, FIRST_BYTE + i))
			return false;
	return true;
}

static int run_test(void)
{
	unsigned int i, round;
	bool control_out = false;

	ksft_print_header();
	if (geteuid())
		ksft_exit_skip("root is required for memory.reclaim\n");
	if (access(CGROUP_ROOT "/memory.reclaim", W_OK))
		ksft_exit_skip("cgroup v2 memory.reclaim is unavailable\n");
	if (!enter_cgroup())
		ksft_exit_skip("cannot create or enter a test memory cgroup: %s\n",
			       strerror(errno));
	atexit(leave_cgroup);

	ksft_set_plan((ARRAY_SIZE(windows) - 1) * 2);
	for (i = 0; i < ARRAY_SIZE(windows); i++) {
		windows[i].usable = setup_window(&windows[i]);
		if (!windows[i].usable)
			ksft_print_msg("%s: setup failed: %s\n", windows[i].name,
				       strerror(errno));
	}
	if (!windows[0].usable)
		ksft_exit_fail_msg("control window setup failed\n");

	for (round = 0; round < RECLAIM_ROUNDS && !control_out; round++) {
		reclaim_round();
		control_out = !resident_slices(&windows[0]);
	}
	if (!control_out) {
		leave_cgroup();
		ksft_exit_skip("memory.reclaim never swapped out the control window (is swap active?)\n");
	}
	/* A few more rounds give every split window the same chances. */
	for (round = 0; round < 4; round++)
		reclaim_round();

	for (i = 1; i < ARRAY_SIZE(windows); i++) {
		unsigned int resident;

		if (!windows[i].usable) {
			ksft_test_result_skip("%s is swapped out\n", windows[i].name);
			continue;
		}
		resident = resident_slices(&windows[i]);
		if (resident)
			ksft_print_msg("%s: %u slice(s) still resident\n",
				       windows[i].name, resident);
		ksft_test_result(!resident, "%s is swapped out\n",
				 windows[i].name);
	}
	for (i = 0; i < ARRAY_SIZE(windows); i++) {
		if (!i) {
			if (!contents_intact(&windows[0]))
				ksft_exit_fail_msg("control window lost data\n");
			continue;
		}
		if (!windows[i].usable) {
			ksft_test_result_skip("%s keeps its data\n", windows[i].name);
			continue;
		}
		ksft_test_result(contents_intact(&windows[i]), "%s keeps its data\n",
				 windows[i].name);
	}
	leave_cgroup();
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
