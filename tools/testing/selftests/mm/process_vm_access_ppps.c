// SPDX-License-Identifier: GPL-2.0
/*
 * process_vm_readv()/process_vm_writev() from a native (16K) caller transfer
 * two 4K pages of a 4K compat child, both anonymous and file-backed at a
 * 4K-only file offset, with the contents intact in each direction.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define TRANSFER_SIZE	(2 * PROCESS_PAGE_SIZE)
#define MAPPING_SIZE	(16 * PROCESS_PAGE_SIZE)
#define FILE_MAPPING_OFFSET	(3 * PROCESS_PAGE_SIZE)
#define FILE_SIZE	(16 * PROCESS_PAGE_SIZE)

struct child_info {
	uintptr_t anon_address;
	uintptr_t file_address;
	long page_size;
};

static uintptr_t align_up(uintptr_t value, size_t alignment)
{
	return (value + alignment - 1) & ~(uintptr_t)(alignment - 1);
}

static bool buffer_is_value(const unsigned char *buffer, size_t length,
			    unsigned char value)
{
	size_t i;

	for (i = 0; i < length; i++) {
		if (buffer[i] != value)
			return false;
	}
	return true;
}

static int run_child(int info_fd, int command_fd)
{
	unsigned char file_contents[TRANSFER_SIZE];
	unsigned char *file_mapping;
	unsigned char *mapping;
	unsigned char *target;
	struct child_info info;
	char command;
	bool passed;
	int memfd;

	mapping = mmap(NULL, MAPPING_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED) {
		perror("child mmap");
		return EXIT_FAILURE;
	}
	target = (unsigned char *)align_up((uintptr_t)mapping, NATIVE_PAGE_SIZE);
	if (target + TRANSFER_SIZE > mapping + MAPPING_SIZE)
		return EXIT_FAILURE;
	memset(target, 0x11, PROCESS_PAGE_SIZE);
	memset(target + PROCESS_PAGE_SIZE, 0x22, PROCESS_PAGE_SIZE);

	memfd = memfd_create("process-vm-access-ppps", 0);
	if (memfd < 0 || ftruncate(memfd, FILE_SIZE)) {
		perror("child memfd setup");
		return EXIT_FAILURE;
	}
	memset(file_contents, 0x55, PROCESS_PAGE_SIZE);
	memset(file_contents + PROCESS_PAGE_SIZE, 0x66, PROCESS_PAGE_SIZE);
	if (!pwrite_full(memfd, file_contents, sizeof(file_contents),
			 FILE_MAPPING_OFFSET)) {
		perror("child pwrite");
		return EXIT_FAILURE;
	}
	file_mapping = mmap(NULL, TRANSFER_SIZE, PROT_READ | PROT_WRITE,
			    MAP_SHARED, memfd, FILE_MAPPING_OFFSET);
	if (file_mapping == MAP_FAILED) {
		perror("child file mmap");
		return EXIT_FAILURE;
	}

	info.anon_address = (uintptr_t)target;
	info.file_address = (uintptr_t)file_mapping;
	info.page_size = sysconf(_SC_PAGESIZE);
	if (!write_full(info_fd, &info, sizeof(info)))
		return EXIT_FAILURE;
	if (!read_full(command_fd, &command, sizeof(command)))
		return EXIT_FAILURE;

	passed = buffer_is_value(target, PROCESS_PAGE_SIZE, 0x33) &&
		 buffer_is_value(target + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
				 0x44) &&
		 buffer_is_value(file_mapping, PROCESS_PAGE_SIZE, 0x77) &&
		 buffer_is_value(file_mapping + PROCESS_PAGE_SIZE,
				 PROCESS_PAGE_SIZE, 0x88);
	munmap(file_mapping, TRANSFER_SIZE);
	close(memfd);
	munmap(mapping, MAPPING_SIZE);
	return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int run_parent(void)
{
	unsigned char read_buffer[TRANSFER_SIZE];
	unsigned char write_buffer[TRANSFER_SIZE];
	struct child_info info;
	struct iovec local;
	struct iovec remote;
	ssize_t transferred;
	int command_pipe[2];
	int info_pipe[2];
	int child_status;
	char command = 1;
	pid_t child;
	bool read_data_ok;

	ksft_print_header();
	ksft_set_plan(8);
	if (pipe(info_pipe) || pipe(command_pipe))
		ksft_exit_fail_msg("pipe failed: %s\n", strerror(errno));
	child = fork();
	if (child < 0)
		ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
	if (!child) {
		char command_fd[16];
		char info_fd[16];

		close(info_pipe[0]);
		close(command_pipe[1]);
		snprintf(info_fd, sizeof(info_fd), "%d", info_pipe[1]);
		snprintf(command_fd, sizeof(command_fd), "%d", command_pipe[0]);
		ppps_execl(true, NULL, "--child", info_fd, command_fd, NULL);
		_exit(127);
	}

	close(info_pipe[1]);
	close(command_pipe[0]);
	if (!read_full(info_pipe[0], &info, sizeof(info)))
		ksft_exit_fail_msg("failed to read child mapping information\n");
	close(info_pipe[0]);
	ksft_print_msg("target page size=%ld, anon=%#lx, file=%#lx\n",
		       info.page_size, (unsigned long)info.anon_address,
		       (unsigned long)info.file_address);
	ksft_test_result(info.page_size == PROCESS_PAGE_SIZE &&
			 !(info.anon_address & (NATIVE_PAGE_SIZE - 1)),
			 "target exposes two 4K pages at a native-page boundary\n");

	memset(read_buffer, 0, sizeof(read_buffer));
	local.iov_base = read_buffer;
	local.iov_len = sizeof(read_buffer);
	remote.iov_base = (void *)info.anon_address;
	remote.iov_len = sizeof(read_buffer);
	errno = 0;
	transferred = process_vm_readv(child, &local, 1, &remote, 1, 0);
	if (transferred != TRANSFER_SIZE)
		ksft_print_msg("process_vm_readv returned %zd: %s\n", transferred,
			       transferred < 0 ? strerror(errno) : "short read");
	ksft_test_result(transferred == TRANSFER_SIZE,
			 "process_vm_readv transfers both anonymous 4K pages\n");
	read_data_ok = buffer_is_value(read_buffer, PROCESS_PAGE_SIZE, 0x11) &&
		       buffer_is_value(read_buffer + PROCESS_PAGE_SIZE,
				       PROCESS_PAGE_SIZE, 0x22);
	ksft_test_result(read_data_ok,
			 "process_vm_readv preserves anonymous 4K page contents\n");

	memset(write_buffer, 0x33, PROCESS_PAGE_SIZE);
	memset(write_buffer + PROCESS_PAGE_SIZE, 0x44, PROCESS_PAGE_SIZE);
	local.iov_base = write_buffer;
	local.iov_len = sizeof(write_buffer);
	errno = 0;
	transferred = process_vm_writev(child, &local, 1, &remote, 1, 0);
	if (transferred != TRANSFER_SIZE)
		ksft_print_msg("process_vm_writev returned %zd: %s\n", transferred,
			       transferred < 0 ? strerror(errno) : "short write");
	ksft_test_result(transferred == TRANSFER_SIZE,
			 "process_vm_writev transfers both anonymous 4K pages\n");

	memset(read_buffer, 0, sizeof(read_buffer));
	local.iov_base = read_buffer;
	local.iov_len = sizeof(read_buffer);
	remote.iov_base = (void *)info.file_address;
	errno = 0;
	transferred = process_vm_readv(child, &local, 1, &remote, 1, 0);
	if (transferred != TRANSFER_SIZE)
		ksft_print_msg("file process_vm_readv returned %zd: %s\n",
			       transferred,
			       transferred < 0 ? strerror(errno) : "short read");
	ksft_test_result(transferred == TRANSFER_SIZE,
			 "process_vm_readv transfers both file-backed 4K pages\n");
	read_data_ok = buffer_is_value(read_buffer, PROCESS_PAGE_SIZE, 0x55) &&
		       buffer_is_value(read_buffer + PROCESS_PAGE_SIZE,
				       PROCESS_PAGE_SIZE, 0x66);
	ksft_test_result(read_data_ok,
			 "process_vm_readv honors the file VMA slice offset\n");

	memset(write_buffer, 0x77, PROCESS_PAGE_SIZE);
	memset(write_buffer + PROCESS_PAGE_SIZE, 0x88, PROCESS_PAGE_SIZE);
	local.iov_base = write_buffer;
	local.iov_len = sizeof(write_buffer);
	errno = 0;
	transferred = process_vm_writev(child, &local, 1, &remote, 1, 0);
	if (transferred != TRANSFER_SIZE)
		ksft_print_msg("file process_vm_writev returned %zd: %s\n",
			       transferred,
			       transferred < 0 ? strerror(errno) : "short write");
	ksft_test_result(transferred == TRANSFER_SIZE,
			 "process_vm_writev transfers both file-backed 4K pages\n");

	if (!write_full(command_pipe[1], &command, sizeof(command)))
		ksft_print_msg("failed to release child: %s\n", strerror(errno));
	close(command_pipe[1]);
	if (waitpid(child, &child_status, 0) < 0)
		ksft_exit_fail_msg("waitpid failed: %s\n", strerror(errno));
	ksft_test_result(WIFEXITED(child_status) &&
			 WEXITSTATUS(child_status) == EXIT_SUCCESS,
			 "process_vm_writev updates anonymous and file-backed pages\n");

	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode)
		exec_native(argv[0], "--run", NULL);
	if (argc == 2 && !strcmp(mode, "--run"))
		return run_parent();
	if (argc == 4 && !strcmp(mode, "--child"))
		return run_child(atoi(argv[2]), atoi(argv[3]));
	return EXIT_FAILURE;
}
