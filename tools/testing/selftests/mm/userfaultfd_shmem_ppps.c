// SPDX-License-Identifier: GPL-2.0
/* Verify that 4K compat userfaultfd rejects shared shmem mappings. */
#define _GNU_SOURCE

#include <linux/memfd.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

static int run_test(void)
{
	struct uffdio_register registration = {
		.mode = UFFDIO_REGISTER_MODE_MISSING,
	};
	struct uffdio_api api = {
		.api = UFFD_API,
	};
	void *mapping;
	int memfd;
	int uffd;

	ksft_print_header();
	ksft_set_plan(3);

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd < 0) {
		if (errno == EPERM)
			ksft_exit_skip("userfaultfd is unavailable: %s\n",
				       strerror(errno));
		ksft_exit_fail_msg("userfaultfd failed: %s\n", strerror(errno));
	}
	ksft_test_result(!ioctl(uffd, UFFDIO_API, &api),
			 "enable the userfaultfd API\n");

	memfd = memfd_create("userfaultfd-shmem-ppps", MFD_CLOEXEC);
	if (memfd < 0 || ftruncate(memfd, PROCESS_PAGE_SIZE))
		ksft_exit_fail_msg("memfd setup failed: %s\n", strerror(errno));
	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, memfd, 0);
	ksft_test_result(mapping != MAP_FAILED, "map shared shmem\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("shmem mmap failed: %s\n", strerror(errno));

	registration.range.start = (unsigned long)mapping;
	registration.range.len = PROCESS_PAGE_SIZE;
	errno = 0;
	ksft_test_result(ioctl(uffd, UFFDIO_REGISTER, &registration) == -1 &&
			 errno == EOPNOTSUPP,
			 "reject shared shmem with EOPNOTSUPP\n");

	munmap(mapping, PROCESS_PAGE_SIZE);
	close(memfd);
	close(uffd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
