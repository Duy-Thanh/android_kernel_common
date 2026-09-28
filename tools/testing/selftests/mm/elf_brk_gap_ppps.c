// SPDX-License-Identifier: GPL-2.0
/*
 * brk randomization of an AArch32 ET_EXEC run from a 4K compat process
 * works in 4K steps: the initial brk gap can be smaller than a native 16K
 * page.
 */
#define _GNU_SOURCE

#include <elf.h>
#include <linux/memfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#ifndef PT_GNU_STACK
#define PT_GNU_STACK 0x6474e551
#endif

#define MAX_SAMPLES 65536U

/*
 * A libc-free AArch32 ET_EXEC image lets arch_randomize_brk() use its 32 MiB
 * compat range. Its sole PT_LOAD ends at 0x10134, so the initial aligned brk
 * base is 0x11000. The payload writes brk(0) as one little-endian u32 to fd 3.
 *
 * A correct 4K process can return base + 4K, +8K, or +12K. Adding a native
 * 16K PAGE_SIZE before randomization makes all three values impossible. With
 * 65536 samples, the chance that a correct kernel misses all three values is
 * less than 4e-11. Embedding the ELF avoids a 32-bit libc or compiler build
 * dependency.
 */
#define SAMPLE_LOAD_BASE 0x10000U
#define SAMPLE_CODE_OFFSET 0x100U
#define SAMPLE_FILE_SIZE 0x134U
#define SAMPLE_BRK_BASE 0x11000U

static const unsigned char sample_code[] = {
	0x00, 0x00, 0xa0, 0xe3, /* mov r0, #0 */
	0x2d, 0x70, 0xa0, 0xe3, /* mov r7, #__NR_brk */
	0x00, 0x00, 0x00, 0xef, /* svc #0 */
	0x08, 0xd0, 0x4d, 0xe2, /* sub sp, sp, #8 */
	0x00, 0x00, 0x8d, 0xe5, /* str r0, [sp] */
	0x03, 0x00, 0xa0, 0xe3, /* mov r0, #3 */
	0x0d, 0x10, 0xa0, 0xe1, /* mov r1, sp */
	0x04, 0x20, 0xa0, 0xe3, /* mov r2, #4 */
	0x04, 0x70, 0xa0, 0xe3, /* mov r7, #__NR_write */
	0x00, 0x00, 0x00, 0xef, /* svc #0 */
	0x00, 0x00, 0xa0, 0xe3, /* mov r0, #0 */
	0x01, 0x70, 0xa0, 0xe3, /* mov r7, #__NR_exit */
	0x00, 0x00, 0x00, 0xef, /* svc #0 */
};

static int make_sample(void)
{
	union {
		Elf32_Ehdr alignment;
		unsigned char bytes[SAMPLE_FILE_SIZE];
	} image = {};
	Elf32_Ehdr *ehdr = (Elf32_Ehdr *)image.bytes;
	Elf32_Phdr *phdr = (Elf32_Phdr *)(image.bytes + sizeof(*ehdr));
	int fd, highfd;

	_Static_assert(SAMPLE_CODE_OFFSET + sizeof(sample_code) ==
		       SAMPLE_FILE_SIZE, "unexpected AArch32 payload size");

	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = ELFCLASS32;
	ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELFOSABI_NONE;
	ehdr->e_type = ET_EXEC;
	ehdr->e_machine = EM_ARM;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_entry = SAMPLE_LOAD_BASE + SAMPLE_CODE_OFFSET;
	ehdr->e_phoff = sizeof(*ehdr);
	ehdr->e_flags = 0x05000000; /* EF_ARM_EABI_VER5 */
	ehdr->e_ehsize = sizeof(*ehdr);
	ehdr->e_phentsize = sizeof(*phdr);
	ehdr->e_phnum = 2;

	phdr[0].p_type = PT_LOAD;
	phdr[0].p_offset = 0;
	phdr[0].p_vaddr = SAMPLE_LOAD_BASE;
	phdr[0].p_paddr = SAMPLE_LOAD_BASE;
	phdr[0].p_filesz = SAMPLE_FILE_SIZE;
	phdr[0].p_memsz = SAMPLE_FILE_SIZE;
	phdr[0].p_flags = PF_R | PF_X;
	phdr[0].p_align = PROCESS_PAGE_SIZE;
	phdr[1].p_type = PT_GNU_STACK;
	phdr[1].p_flags = PF_R | PF_W;
	phdr[1].p_align = 16;
	memcpy(image.bytes + SAMPLE_CODE_OFFSET, sample_code,
	       sizeof(sample_code));

	fd = syscall(SYS_memfd_create, "elf-brk-gap-ppps", MFD_CLOEXEC);
	if (fd < 0)
		return -1;
	if (!write_full(fd, image.bytes, sizeof(image.bytes)) || fchmod(fd, 0700)) {
		close(fd);
		return -1;
	}

	/* Keep fd 3 available for the payload's result pipe. */
	highfd = fcntl(fd, F_DUPFD_CLOEXEC, 10);
	close(fd);
	return highfd;
}

static int collect_sample(int sample_fd, uint32_t *brk)
{
	char *const argv[] = { "elf-brk-gap-ppps-compat", NULL };
	char *const envp[] = { NULL };
	int pipefd[2], status;
	pid_t pid;

	if (pipe(pipefd))
		return -errno;
	pid = fork();
	if (pid < 0) {
		int error = errno;

		close(pipefd[0]);
		close(pipefd[1]);
		return -error;
	}
	if (!pid) {
		uint32_t exec_error;

		close(pipefd[0]);
		if (pipefd[1] != 3) {
			if (dup2(pipefd[1], 3) < 0)
				_exit(126);
			close(pipefd[1]);
		}
		syscall(SYS_execveat, sample_fd, "", argv, envp, AT_EMPTY_PATH);
		exec_error = 0x80000000U | (uint32_t)errno;
		write_full(3, &exec_error, sizeof(exec_error));
		_exit(127);
	}

	close(pipefd[1]);
	if (!read_full(pipefd[0], brk, sizeof(*brk))) {
		close(pipefd[0]);
		waitpid(pid, &status, 0);
		return -EIO;
	}
	close(pipefd[0]);
	if (waitpid(pid, &status, 0) != pid)
		return -errno;
	if (*brk & 0x80000000U)
		return -(int)(*brk & 0x7fffffffU);
	if (!WIFEXITED(status) || WEXITSTATUS(status))
		return -ECHILD;
	return 0;
}

static bool brk_aslr_enabled(void)
{
	int randomize_va_space;
	FILE *file;

	file = fopen("/proc/sys/kernel/randomize_va_space", "r");
	if (!file)
		return false;
	if (fscanf(file, "%d", &randomize_va_space) != 1)
		randomize_va_space = 0;
	fclose(file);
	return randomize_va_space > 1;
}

static int run_test(void)
{
	uint32_t minimum_gap = UINT32_MAX;
	unsigned int samples = 0;
	bool valid_samples = true;
	bool found_low_gap = false;
	int sample_fd;

	ksft_print_header();
	if (!brk_aslr_enabled())
		ksft_exit_skip("brk ASLR is disabled\n");
	ksft_set_plan(2);

	sample_fd = make_sample();
	if (sample_fd < 0)
		ksft_exit_fail_msg("failed to create compat ELF: %s\n",
				   strerror(errno));

	while (samples < MAX_SAMPLES) {
		uint32_t brk, gap;
		int ret = collect_sample(sample_fd, &brk);

		if (ret == -ENOEXEC) {
			close(sample_fd);
			ksft_exit_skip("CPU does not support AArch32 EL0\n");
		}
		if (ret) {
			ksft_print_msg("sample %u failed: %s\n", samples,
				       strerror(-ret));
			valid_samples = false;
			break;
		}
		samples++;
		if (brk < SAMPLE_BRK_BASE ||
		    (brk - SAMPLE_BRK_BASE) % PROCESS_PAGE_SIZE) {
			ksft_print_msg("invalid sample brk=%#x\n", brk);
			valid_samples = false;
			break;
		}
		gap = brk - SAMPLE_BRK_BASE;
		if (gap < minimum_gap)
			minimum_gap = gap;
		if (gap < NATIVE_PAGE_SIZE) {
			found_low_gap = true;
			break;
		}
	}
	close(sample_fd);

	ksft_test_result(valid_samples && samples,
			 "collect valid AArch32 brk samples\n");
	ksft_print_msg("samples=%u minimum_gap=%#x\n", samples, minimum_gap);
	ksft_test_result(found_low_gap,
			 "initial brk gap can be smaller than 16K\n");
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
