// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "kselftest_ppps.h"

#ifndef MSG_ZEROCOPY
#define MSG_ZEROCOPY 0x4000000
#endif

/* Compat zero-copy must either preserve data or fail explicitly. */
static int run_test(void)
{
	struct sockaddr_in sa = { .sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
	socklen_t sl = sizeof(sa);
	const size_t bytes = 64 * 1024;
	unsigned char *data_res, *data, *area, scratch[NATIVE_PAGE_SIZE];
	int listener, sender, receiver, one = 1, mss = NATIVE_PAGE_SIZE + 12;
	int socket_buffer = 1024 * 1024;
	int result = KSFT_SKIP;
	size_t sent = 0, consumed = 0;
	bool compat = getpagesize() == PROCESS_PAGE_SIZE;

	ksft_print_header();
	ksft_set_plan(compat ? 4 : 1);
	alarm(20);
	listener = socket(AF_INET, SOCK_STREAM, 0);
	sender = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0 || sender < 0)
		ksft_exit_fail_msg("socket: %s\n", strerror(errno));
	/* Send finishes before receive begins: reserve room for the bounded input. */
	if (setsockopt(listener, SOL_SOCKET, SO_RCVBUF, &socket_buffer,
		       sizeof(socket_buffer)) ||
	    setsockopt(sender, SOL_SOCKET, SO_SNDBUF, &socket_buffer,
		       sizeof(socket_buffer)))
		ksft_exit_fail_msg("socket buffers: %s\n", strerror(errno));
	setsockopt(listener, IPPROTO_TCP, TCP_MAXSEG, &mss, sizeof(mss));
	setsockopt(sender, IPPROTO_TCP, TCP_MAXSEG, &mss, sizeof(mss));
	setsockopt(sender, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	if (setsockopt(sender, SOL_SOCKET, SO_ZEROCOPY, &one, sizeof(one)))
		ksft_exit_skip("SO_ZEROCOPY unavailable: %s\n", strerror(errno));
	if (bind(listener, (void *)&sa, sl) || listen(listener, 1) ||
	    getsockname(listener, (void *)&sa, &sl) ||
	    connect(sender, (void *)&sa, sl))
		ksft_exit_fail_msg("connect: %s\n", strerror(errno));
	receiver = accept(listener, NULL, NULL);
	if (receiver < 0)
		ksft_exit_fail_msg("setup: %s\n", strerror(errno));
	data_res = mmap(NULL, bytes + 2 * NATIVE_PAGE_SIZE,
		    PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (data_res == MAP_FAILED)
		ksft_exit_fail_msg("data mmap: %s\n", strerror(errno));
	data = (unsigned char *)(((uintptr_t)data_res + NATIVE_PAGE_SIZE - 1) &
				~(uintptr_t)(NATIVE_PAGE_SIZE - 1));
	if (getpagesize() == PROCESS_PAGE_SIZE)
		data += PROCESS_PAGE_SIZE;
	area = mmap(NULL, bytes + NATIVE_PAGE_SIZE, PROT_READ, MAP_SHARED,
		    receiver, 0);
	if (area == MAP_FAILED)
		ksft_exit_fail_msg("mmap: %s\n", strerror(errno));
	if (compat) {
		struct tcp_zerocopy_receive zc = {
			.address = ((uintptr_t)area + NATIVE_PAGE_SIZE - 1) &
				   ~(uintptr_t)(NATIVE_PAGE_SIZE - 1),
			.length = bytes,
		};
		socklen_t len = sizeof(zc);
		int ret;

		ksft_test_result_pass("TCP mmap remains available in compat mode\n");
		ret = getsockopt(receiver, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE, &zc, &len);
		ksft_test_result(!ret || (ret == -1 && errno == EOPNOTSUPP),
				 "empty compat zero-copy succeeds or is explicitly unsupported\n");
	}
	madvise(data, bytes, MADV_NOHUGEPAGE);
	for (size_t page = 0; page < bytes / PROCESS_PAGE_SIZE; page++)
		memset(data + page * PROCESS_PAGE_SIZE, 0x40 + page,
		       PROCESS_PAGE_SIZE);
	while (sent < bytes) {
		ssize_t n = send(sender, data + sent, bytes - sent, MSG_ZEROCOPY);
		if (n <= 0)
			ksft_exit_fail_msg("send zerocopy: %s\n", strerror(errno));
		sent += n;
	}
	shutdown(sender, SHUT_WR);
	if (compat) {
		unsigned char *received = malloc(bytes);
		struct tcp_zerocopy_receive zc = {
			.address = ((uintptr_t)area + NATIVE_PAGE_SIZE - 1) &
				   ~(uintptr_t)(NATIVE_PAGE_SIZE - 1),
			.length = bytes,
		};
		socklen_t len = sizeof(zc);
		bool supported;
		int ret;

		if (!received)
			ksft_exit_fail_msg("receive buffer allocation\n");
		ret = getsockopt(receiver, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE, &zc, &len);
		supported = ret == 0;
		ksft_test_result(supported || (ret == -1 && errno == EOPNOTSUPP),
				 "compat zero-copy succeeds or returns EOPNOTSUPP\n");
		if (supported) {
			if (zc.length > bytes)
				ksft_exit_fail_msg("invalid zero-copy length: %u\n",
						   zc.length);
			if (zc.length)
				memcpy(received, (void *)(uintptr_t)zc.address,
				       zc.length);
			consumed = zc.length;
		}
		while (consumed < bytes) {
			ssize_t n = recv(receiver, received + consumed, bytes - consumed, 0);

			if (n <= 0)
				break;
			consumed += n;
		}
		ksft_test_result(consumed == bytes && !memcmp(received, data, bytes),
				 "compat zero-copy or explicit rejection preserves every byte\n");
		free(received);
		goto out;
	}
	while (consumed < bytes) {
		struct tcp_zerocopy_receive zc = {
			.address = ((uintptr_t)area + NATIVE_PAGE_SIZE - 1) &
				   ~(uintptr_t)(NATIVE_PAGE_SIZE - 1),
			.length = bytes,
		};
		socklen_t len = sizeof(zc);
		int ret = getsockopt(receiver, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE,
				     &zc, &len);
		int error = errno;

		ksft_print_msg("zc ret=%d errno=%d length=%u skip=%u\n",
			       ret, ret ? error : 0, zc.length, zc.recv_skip_hint);
		if (ret && error == EOPNOTSUPP) {
			result = getpagesize() == PROCESS_PAGE_SIZE ? KSFT_PASS : KSFT_FAIL;
			break;
		}
		if (ret) {
			result = KSFT_FAIL;
			break;
		}
		if (zc.length) {
			unsigned char *mapped = (void *)(uintptr_t)zc.address;
			size_t mismatch;

			for (mismatch = 0; mismatch < zc.length; mismatch++)
				if (mapped[mismatch] != data[mismatch])
					break;
			if (mismatch != zc.length)
				ksft_print_msg("first data mismatch at %#zx: mapped=%#x expected=%#x\n",
					       mismatch, mapped[mismatch], data[mismatch]);
			result = mismatch == zc.length ? KSFT_PASS : KSFT_FAIL;
			break;
		}
		if (zc.recv_skip_hint) {
			size_t left = zc.recv_skip_hint;
			while (left) {
				ssize_t n = read(receiver, scratch,
					left < sizeof(scratch) ? left : sizeof(scratch));
				if (n <= 0)
					ksft_exit_fail_msg("read: %s\n", strerror(errno));
				left -= n;
				consumed += n;
			}
		} else {
			usleep(1000);
		}
	}
	if (result == KSFT_SKIP)
		ksft_test_result_skip("loopback supplied no mappable fragments\n");
	else
		ksft_test_result(result == KSFT_PASS,
			"native zero-copy preserves every byte\n");
out:
	munmap(area, bytes + NATIVE_PAGE_SIZE);
	munmap(data_res, bytes + 2 * NATIVE_PAGE_SIZE);
	close(receiver);
	close(sender);
	close(listener);
	ksft_finished();
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--native")) {
		if (getpagesize() != NATIVE_PAGE_SIZE)
			exec_native(argv[0], "--native", NULL);
		return run_test();
	}
	return ppps_compat_main(argc, argv, run_test);
}
