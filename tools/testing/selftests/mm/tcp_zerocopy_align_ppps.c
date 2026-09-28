// SPDX-License-Identifier: GPL-2.0
/*
 * PPPS reproducer: TCP receive-zerocopy entry-alignment regression.
 *
 * tcp_zerocopy_receive() remaps received pages into the caller's VMA at the
 * kernel's native PAGE_SIZE granularity (it zaps and inserts in PAGE_SIZE
 * steps, i.e. 16K on a 16K-native kernel).  The PPPS series relaxed the entry
 * alignment gate to MM_PAGE_ALIGNED(current->mm, address), which for a compat
 * (4K) process only demands 4K alignment.  A 4K-aligned-but-not-16K-aligned
 * address was therefore accepted and the receive then proceeded at 16K
 * granularity, corrupting the destination layout.  The audit fix restores an
 * unconditional native-PAGE_SIZE requirement:
 *
 *     if (address & (PAGE_SIZE - 1) || address != zc->address)
 *             return -EINVAL;
 *
 * Discriminator.  On a connected socket with an empty receive queue the call
 * returns at the `inq < PAGE_SIZE` early-exit, *before* find_tcp_vma(), so the
 * only code that runs is the alignment gate.  We therefore need neither real
 * data flow nor a real TCP-mapped VMA -- just a connected fd and two probe
 * addresses:
 *
 *   - 16K-aligned address        -> accepted on every kernel (getsockopt 0)
 *   - 4K-aligned, not 16K-aligned:
 *         fixed kernel           -> EINVAL   (regression closed)
 *         baseline (buggy) kernel-> 0        (sub-native alignment accepted)
 *
 * The bug is only observable from a compat (4K) process; on a native process
 * MM_PAGE_ALIGNED already means 16K and both kernels reject.  The test
 * re-execs itself as a compat process before probing.
 *
 * This binary is self-validating as an A/B: run it on the audited (fixed)
 * kernel to see the PASS, and on the pre-fix kernel to see it report the
 * regression.
 */
#define _GNU_SOURCE

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/socket.h>

#include "kselftest_ppps.h"

#ifndef TCP_ZEROCOPY_RECEIVE
#define TCP_ZEROCOPY_RECEIVE	35
#endif

/* uapi struct tcp_zerocopy_receive, replicated to avoid header conflicts. */
struct zc_receive {
	uint64_t address;
	uint32_t length;
	uint32_t recv_skip_hint;
	uint32_t inq;
	int32_t  err;
	uint64_t copybuf_address;
	int32_t  copybuf_len;
	uint32_t flags;
	uint64_t msg_control;
	uint64_t msg_controllen;
	uint32_t msg_flags;
	uint32_t reserved;
};

/* Bring up a connected loopback TCP socket; returns the connected client fd. */
static int connected_loopback_fd(void)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
		.sin_port = 0,
	};
	socklen_t alen = sizeof(addr);
	int lfd, cfd, afd;

	lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0)
		ksft_exit_fail_perror("socket(listen)");
	if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)))
		ksft_exit_fail_perror("bind");
	if (listen(lfd, 1))
		ksft_exit_fail_perror("listen");
	if (getsockname(lfd, (struct sockaddr *)&addr, &alen))
		ksft_exit_fail_perror("getsockname");

	cfd = socket(AF_INET, SOCK_STREAM, 0);
	if (cfd < 0)
		ksft_exit_fail_perror("socket(client)");
	if (connect(cfd, (struct sockaddr *)&addr, sizeof(addr)))
		ksft_exit_fail_perror("connect");

	afd = accept(lfd, NULL, NULL);
	if (afd < 0)
		ksft_exit_fail_perror("accept");

	close(lfd);
	/*
	 * Leave the accepted peer fd open (never written to): the receive
	 * queue stays empty (inq == 0) so the call hits the early return
	 * after the alignment gate, and the peer staying alive keeps
	 * SOCK_DONE clear so an accepted address returns 0 rather than -EIO.
	 */
	(void)afd;
	return cfd;
}

/*
 * Invoke TCP_ZEROCOPY_RECEIVE for @address on a connected, empty socket.
 * Returns 0 if the kernel accepted the address, or -errno if it rejected it.
 */
static int zc_probe(int fd, unsigned long address)
{
	struct zc_receive zc;
	socklen_t len = sizeof(zc);

	memset(&zc, 0, sizeof(zc));
	zc.address = address;
	zc.length = NATIVE_PAGE_SIZE;

	if (getsockopt(fd, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE, &zc, &len))
		return -errno;
	return 0;
}

static int run_test(void)
{
	unsigned char *region, *base16k, *probe4k;
	int fd, r_aligned, r_misaligned;
	size_t maplen = NATIVE_PAGE_SIZE * 4;

	ksft_print_header();
	ksft_set_plan(1);

	/* A 16K-aligned window, and a 4K-aligned probe one compat page inside. */
	region = mmap(NULL, maplen, PROT_READ,
		      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (region == MAP_FAILED)
		ksft_exit_fail_perror("mmap");
	base16k = (unsigned char *)(((uintptr_t)region + NATIVE_PAGE_SIZE - 1) &
				    ~(uintptr_t)(NATIVE_PAGE_SIZE - 1));
	probe4k = base16k + PROCESS_PAGE_SIZE;	/* 4K-aligned, not 16K-aligned */

	fd = connected_loopback_fd();

	/* Control: a properly native-aligned address must be accepted. */
	r_aligned = zc_probe(fd, (unsigned long)base16k);
	if (r_aligned != 0) {
		ksft_test_result_skip(
			"16K-aligned control was rejected (%d); cannot isolate the alignment gate\n",
			r_aligned);
		ksft_finished();
	}

	/* Probe: the sub-native-aligned address is what the fix must reject. */
	r_misaligned = zc_probe(fd, (unsigned long)probe4k);

	if (r_misaligned == -EINVAL) {
		ksft_test_result_pass(
			"4K-aligned (non-16K) TCP_ZEROCOPY_RECEIVE address rejected with EINVAL (fix present)\n");
	} else if (r_misaligned == 0) {
		ksft_test_result_fail(
			"4K-aligned (non-16K) address accepted -- baseline zerocopy alignment regression\n");
	} else {
		ksft_test_result_fail(
			"4K-aligned address gave unexpected result %d (expected -EINVAL)\n",
			r_misaligned);
	}

	close(fd);
	munmap(region, maplen);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
