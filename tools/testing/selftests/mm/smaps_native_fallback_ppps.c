// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
/*
 * A native 16K process accounts a file page in smaps from its mapcount, as on
 * a native kernel: every PTE that maps the page, native or 4K compat, counts
 * as one sharer of the whole page, and the native reader never walks the
 * reverse map (P3S does that only for compat VMAs).  Compat views still
 * account each 4K slice.  All mappings use initialized, owned memfd pages and
 * pipe-synchronized peers.
 */
#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define LENGTH (8 * NATIVE_PAGE_SIZE)
#define TARGET (3 * NATIVE_PAGE_SIZE)
#define PRESENT (1ULL << 63)
#define EXCLUSIVE (1ULL << 56)

struct stats {
	unsigned long rss, pss, shared, private;
};

struct view {
	pid_t pid;
	uintptr_t address;
};

struct worker {
	pid_t pid;
	int command, reply;
	struct view view;
};

static void require(bool ok, const char *what)
{
	if (!ok)
		ksft_exit_fail_msg("%s: %s\n", what, strerror(errno));
}

static bool receive(int fd, void *buf, size_t len)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };

	return poll(&pfd, 1, 10000) == 1 && read_full(fd, buf, len);
}

static bool get_stats(struct view view, struct stats *s)
{
	char path[64], *line = NULL;
	unsigned long lo, hi, value;
	size_t cap = 0;
	bool found = false;
	FILE *f;

	memset(s, 0, sizeof(*s));
	snprintf(path, sizeof(path), "/proc/%d/smaps", view.pid);
	f = fopen(path, "re");
	if (!f)
		return false;
	while (getline(&line, &cap, f) > 0) {
		if (sscanf(line, "%lx-%lx", &lo, &hi) == 2) {
			if (found)
				break;
			found = view.address >= lo && view.address < hi;
		} else if (found) {
			if (sscanf(line, "Rss: %lu kB", &value) == 1)
				s->rss = value;
			else if (sscanf(line, "Pss: %lu kB", &value) == 1)
				s->pss = value;
			else if (sscanf(line, "Shared_Clean: %lu kB", &value) == 1 ||
				 sscanf(line, "Shared_Dirty: %lu kB", &value) == 1)
				s->shared += value;
			else if (sscanf(line, "Private_Clean: %lu kB", &value) == 1 ||
				 sscanf(line, "Private_Dirty: %lu kB", &value) == 1)
				s->private += value;
		}
	}
	free(line);
	fclose(f);
	return found;
}

static bool exclusive(struct view view, unsigned long page_size, bool expected)
{
	uint64_t entry = 0;
	char path[64];
	int fd;
	bool ok;

	snprintf(path, sizeof(path), "/proc/%d/pagemap", view.pid);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return false;
	ok = pread(fd, &entry, sizeof(entry), view.address / page_size * 8) == 8;
	close(fd);
	return ok && (entry & PRESENT) && !!(entry & EXCLUSIVE) == expected;
}

/*
 * Native Pss of the mapping when its page at TARGET has @mappers mappings and
 * the other pages are private: smaps sums Pss with 12 fractional bits and
 * prints kB.
 */
static unsigned long native_pss(int mappers)
{
	uint64_t pss = (uint64_t)(LENGTH - NATIVE_PAGE_SIZE) << 12;

	pss += ((uint64_t)NATIVE_PAGE_SIZE << 12) / mappers;
	return pss >> 22;
}

static unsigned long native_shared(int mappers)
{
	return mappers > 1 ? NATIVE_PAGE_SIZE / 1024 : 0;
}

static void check_native(unsigned char *base, unsigned long pss,
			 unsigned long shared, const char *name)
{
	struct stats s;
	struct view view = { getpid(), (uintptr_t)base };
	struct view target = { getpid(), (uintptr_t)base + TARGET };
	bool ok = get_stats(view, &s);

	ok = ok && s.rss == LENGTH / 1024 && s.pss == pss &&
		s.shared == shared && s.private == LENGTH / 1024 - shared &&
		exclusive(view, NATIVE_PAGE_SIZE, true) &&
		exclusive(target, NATIVE_PAGE_SIZE, !shared);
	ksft_test_result(ok, "%s: Rss=%lu Pss=%lu Shared=%lu Private=%lu kB\n",
			 name, s.rss, s.pss, s.shared, s.private);
}

static int worker_main(int fd, int input, int output, int slice, int nr,
		       bool cow, bool native)
{
	size_t length = native ? NATIVE_PAGE_SIZE :
		(cow ? 2 * NATIVE_PAGE_SIZE : nr * PROCESS_PAGE_SIZE);
	unsigned char *map;
	struct view view;
	int release = -1, status;
	pid_t descendant = 0;
	char cmd;

	alarm(40);
	if ((unsigned long)getpagesize() !=
	    (native ? NATIVE_PAGE_SIZE : PROCESS_PAGE_SIZE))
		return 1;
	map = mmap(NULL, length, PROT_READ | PROT_WRITE,
		   cow ? MAP_PRIVATE : MAP_SHARED, fd,
		   TARGET + slice * PROCESS_PAGE_SIZE);
	require(map != MAP_FAILED, "worker mmap");
	/* Compat VMAs reject THP advice because they are already non-THP. */
	if (madvise(map, length, MADV_NOHUGEPAGE))
		require(!native && errno == EINVAL, "worker no THP");
	for (int i = 0; i < nr; i++)
		require(*(volatile unsigned char *)(map + i * PROCESS_PAGE_SIZE) ==
			0x40 + TARGET / PROCESS_PAGE_SIZE + slice + i, "owned data");
	/* Force vma_needs_copy(): clean file PTEs must be copied on fork. */
	if (cow)
		map[NATIVE_PAGE_SIZE] = 0xee;
	view = (struct view) { getpid(), (uintptr_t)map };
	require(write_full(output, &view, sizeof(view)), "worker ready");
	while (read_full(input, &cmd, 1)) {
		if (cmd == 'E')
			_exit(0); /* Exercise exit's teardown, not an explicit munmap. */
		if (cmd == 'Q')
			break;
		if (cmd == 'R') {
			void *dest = mmap(NULL, length, PROT_NONE,
					 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

			require(dest != MAP_FAILED, "reserve mremap destination");
			map = mremap(map, length, length,
				     MREMAP_MAYMOVE | MREMAP_FIXED, dest);
			require(map == dest, "move mapping");
		} else if (cmd == 'P') {
			require(length > PROCESS_PAGE_SIZE && !cow, "partial unmap size");
			require(!munmap(map + length - PROCESS_PAGE_SIZE,
					PROCESS_PAGE_SIZE), "partial unmap");
			length -= PROCESS_PAGE_SIZE;
		} else if (cmd == 'F') {
			int hold[2], ready[2];

			require(cow && !descendant && !pipe(hold) && !pipe(ready),
				"fork setup");
			descendant = fork();
			require(descendant >= 0, "compat fork");
			if (!descendant) {
				close(hold[1]);
				close(ready[0]);
				close(input);
				close(output);
				/* Do not fault the file PTEs: fork must have copied them. */
				require(write_full(ready[1], "r", 1), "descendant ready");
				close(ready[1]);
				read_full(hold[0], &cmd, 1);
				_exit(0);
			}
			close(hold[0]);
			close(ready[1]);
			require(receive(ready[0], &cmd, 1), "wait descendant");
			close(ready[0]);
			release = hold[1];
			require(!munmap(map, length), "unmap original after fork");
			length = 0;
			view.pid = descendant;
		} else {
			return 1;
		}
		view.address = (uintptr_t)map;
		require(write_full(output, &view, sizeof(view)), "worker acknowledgement");
	}
	if (length)
		require(!munmap(map, length), "worker unmap");
	if (descendant) {
		close(release);
		require(waitpid(descendant, &status, 0) == descendant &&
			WIFEXITED(status) && !WEXITSTATUS(status), "descendant exit");
	}
	return 0;
}

static struct worker start_worker_mode(int fd, int slice, int nr, bool cow,
				       bool native)
{
	struct worker w;
	int command[2], reply[2];

	require(!pipe(command) && !pipe(reply), "worker pipes");
	w.pid = fork();
	require(w.pid >= 0, "worker fork");
	if (!w.pid) {
		char args[7][24];
		int values[] = { fd, command[0], reply[1], slice, nr, cow, native };

		close(command[1]);
		close(reply[0]);
		for (unsigned int i = 0; i < 7; i++)
			snprintf(args[i], sizeof(args[i]), "%d", values[i]);
		ppps_execl(!native, NULL, "--worker", args[0], args[1], args[2],
			   args[3], args[4], args[5], args[6], NULL);
		_exit(1);
	}
	close(command[0]);
	close(reply[1]);
	w.command = command[1];
	w.reply = reply[0];
	require(receive(w.reply, &w.view, sizeof(w.view)), "start worker");
	return w;
}

static struct worker start_worker(int fd, int slice, int nr, bool cow)
{
	return start_worker_mode(fd, slice, nr, cow, false);
}

static void command_worker(struct worker *w, char cmd)
{
	require(write_full(w->command, &cmd, 1) &&
		receive(w->reply, &w->view, sizeof(w->view)), "command worker");
}

static void stop_worker(struct worker *w, char cmd)
{
	int status;

	require(write_full(w->command, &cmd, 1), "stop worker");
	close(w->command);
	close(w->reply);
	require(waitpid(w->pid, &status, 0) == w->pid &&
		WIFEXITED(status) && !WEXITSTATUS(status), "worker exited");
}

static void check_compat_view(struct worker *w, int nr, int pss, bool shared)
{
	struct stats s;
	bool ok = get_stats(w->view, &s);

	ksft_test_result(ok && s.rss == nr * 4UL && s.pss == (unsigned long)pss &&
			 s.shared == (shared ? nr * 4UL : 0) &&
			 s.private == (shared ? 0 : nr * 4UL) &&
			 exclusive(w->view, PROCESS_PAGE_SIZE, !shared),
			 "compat view: %d slices, Pss=%lu (expected %d) kB\n",
			 nr, s.pss, pss);
}

static void check_compat(struct worker *w, int nr, int pss)
{
	check_compat_view(w, nr, pss, true);
}

static void check_mixed(unsigned char *base, int native, int compat)
{
	char label[64];

	snprintf(label, sizeof(label), "%d native, %d compat on one slice",
		 native, compat);
	check_native(base, native_pss(native + compat),
		     native_shared(native + compat), label);
}

static void single_compat_counts(int fd, unsigned char *base)
{
	const int sharers[] = { 1, 2, 3, 32 };
	struct worker natives[31], one, two;

	for (unsigned int i = 0; i < ARRAY_SIZE(sharers); i++) {
		int n = sharers[i];

		for (int j = 0; j < n - 1; j++)
			natives[j] = start_worker_mode(fd, 0, 1, false, true);
		/* Use the last slice: the native formula must not assume slice 0. */
		one = start_worker(fd, 3, 1, false);
		check_mixed(base, n, 1);
		check_compat(&one, 1, 4 / (n + 1));
		command_worker(&one, 'R');
		check_mixed(base, n, 1);
		two = start_worker(fd, 3, 1, false);
		check_mixed(base, n, 2);
		check_compat(&one, 1, 4 / (n + 2));
		check_compat(&two, 1, 4 / (n + 2));
		stop_worker(&two, 'E');
		check_mixed(base, n, 1);
		stop_worker(&one, 'E');
		check_native(base, native_pss(n), native_shared(n),
			     "S=1 to S=0 with native peers still present");
		for (int j = 0; j < n - 1; j++)
			stop_worker(&natives[j], 'E');
		check_native(base, 128, 0, "native peers gone");
	}
}

int main(int argc, char **argv)
{
	struct worker w[4];
	unsigned char *base;
	int fd, status;
	pid_t probe;
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (mode && !strcmp(mode, "--probe"))
		return getpagesize() == PROCESS_PAGE_SIZE ? 0 : KSFT_SKIP;
	if (argc == 9 && !strcmp(argv[1], "--worker"))
		return worker_main(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]),
				   atoi(argv[5]), atoi(argv[6]), atoi(argv[7]),
				   atoi(argv[8]));
	if (!mode || strcmp(mode, "--native"))
		exec_native(argv[0], "--native", NULL);
	ksft_print_header();
	if (getpagesize() != NATIVE_PAGE_SIZE)
		ksft_exit_skip("requires a PPPS native 16K kernel\n");
	probe = fork();
	require(probe >= 0, "compat availability probe");
	if (!probe) {
		ppps_execl(true, NULL, "--probe", NULL);
		_exit(1);
	}
	require(waitpid(probe, &status, 0) == probe && WIFEXITED(status),
		"compat probe exited");
	if (WEXITSTATUS(status) == KSFT_SKIP)
		ksft_exit_skip("compat exec is unavailable\n");
	require(!WEXITSTATUS(status), "compat probe succeeded");
	ksft_set_plan(74);
	alarm(120);
	fd = memfd_create("smaps-native-fallback", 0);
	require(fd >= 0 && !ftruncate(fd, LENGTH), "memfd");
	base = mmap(NULL, LENGTH, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	require(base != MAP_FAILED, "native mmap");
	require(!madvise(base, LENGTH, MADV_NOHUGEPAGE), "native no THP");
	for (unsigned int i = 0; i < LENGTH / PROCESS_PAGE_SIZE; i++)
		memset(base + i * PROCESS_PAGE_SIZE, 0x40 + i, PROCESS_PAGE_SIZE);
	check_native(base, 128, 0, "native only");
	for (int nr = 1; nr <= 4; nr++) {
		w[0] = start_worker(fd, 0, nr, false);
		check_native(base, native_pss(1 + nr), native_shared(1 + nr),
			     "one compat peer");
		check_compat(&w[0], nr, 2 * nr);
		if (nr == 4) {
			command_worker(&w[0], 'R');
			check_native(base, native_pss(5), native_shared(5),
				     "mremap preserves sharing");
			for (int left = 3; left; left--) {
				command_worker(&w[0], 'P');
				check_native(base, native_pss(1 + left),
					     native_shared(1 + left),
					     "partial unmap removes only its slices");
			}
		}
		stop_worker(&w[0], 'E');
		check_native(base, 128, 0, "exit restores native accounting");
	}
	for (int i = 0; i < 4; i++)
		w[i] = start_worker(fd, 0, 1, false);
	check_native(base, native_pss(5), native_shared(5),
		     "four peers on the same slice, S=4");
	for (int i = 0; i < 4; i++)
		check_compat(&w[i], 1, 0); /* 4096/5 bytes, truncated to kB by smaps. */
	for (int left = 3; left >= 0; left--) {
		stop_worker(&w[left], 'E');
		check_native(base, native_pss(1 + left), native_shared(1 + left),
			     "successive peer exits");
	}
	for (int i = 0; i < 4; i++)
		w[i] = start_worker(fd, i, 1, false);
	check_native(base, native_pss(5), native_shared(5),
		     "four peers on disjoint slices, also S=4");
	for (int i = 0; i < 4; i++)
		stop_worker(&w[i], 'E');
	check_native(base, 128, 0, "all disjoint peers gone");
	w[0] = start_worker(fd, 0, 4, true);
	check_native(base, native_pss(5), native_shared(5),
		     "private file VMA with separate COW page");
	command_worker(&w[0], 'F');
	check_native(base, native_pss(5), native_shared(5),
		     "forked file PTEs survive original unmap");
	stop_worker(&w[0], 'Q');
	check_native(base, 128, 0, "fork descendant exit restores native accounting");
	w[0] = start_worker(fd, 0, 1, true);
	check_native(base, native_pss(2), native_shared(2),
		     "single file PTE with separate COW page");
	command_worker(&w[0], 'F');
	check_native(base, native_pss(2), native_shared(2),
		     "single forked PTE survives original unmap");
	stop_worker(&w[0], 'Q');
	check_native(base, 128, 0, "single descendant exit restores native accounting");
	single_compat_counts(fd, base);
	munmap(base, LENGTH);
	for (int i = 0; i < 4; i++) {
		w[0] = start_worker(fd, i, 1, false);
		check_compat_view(&w[0], 1, 4, false); /* N=0, S=1: exclusive. */
		stop_worker(&w[0], 'E');
	}
	close(fd);
	ksft_finished();
}
