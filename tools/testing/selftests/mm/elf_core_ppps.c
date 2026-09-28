// SPDX-License-Identifier: GPL-2.0
/*
 * An ELF core dumped by a 4K compat process keeps its 4K VMAs aligned and
 * intact in PT_LOAD, omits a mapping that starts at file offset 4K, and
 * encodes 4K page units in NT_FILE.
 */
#define _GNU_SOURCE

#include <elf.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"
#include "iov_iter_ppps.h"

#define DRIVER_TEST_BASE 0x70000000UL

static int driver_fd = -1;

#define TEST_VMAS	4
#define TEST_BASE	0x50000000UL
#define TEST_STRIDE	0x10000UL
#define FILE_TEST_BASE	0x60000000UL
#define FILE_TEST_PATH	"/tmp/elf-core-ppps-offset-file"
#define CORE_PATTERN	"/tmp/elf-core-ppps-%p"

static bool write_file(const char *path, const char *contents, size_t length)
{
	ssize_t written;
	int fd;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	written = write(fd, contents, length);
	close(fd);
	return written == (ssize_t)length;
}

static bool create_offset_file(void)
{
	int fd;

	fd = open(FILE_TEST_PATH, O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0700);
	if (fd < 0)
		return false;
	if (fchmod(fd, 0700) || ftruncate(fd, 2 * PROCESS_PAGE_SIZE)) {
		close(fd);
		return false;
	}
	close(fd);
	return true;
}

static void crash_with_default_signal(void)
{
	struct sigaction action = {
		.sa_handler = SIG_DFL,
	};
	sigset_t signals;

	sigemptyset(&action.sa_mask);
	if (sigaction(SIGSEGV, &action, NULL))
		_exit(126);
	sigemptyset(&signals);
	sigaddset(&signals, SIGSEGV);
	if (sigprocmask(SIG_UNBLOCK, &signals, NULL))
		_exit(126);
	syscall(SYS_tgkill, getpid(), syscall(SYS_gettid), SIGSEGV);
	_exit(126);
}

static char *read_core_pattern(size_t *length)
{
	char *pattern = NULL;
	size_t capacity = 0;
	ssize_t size;
	FILE *file;

	file = fopen("/proc/sys/kernel/core_pattern", "re");
	if (!file)
		return NULL;
	size = getline(&pattern, &capacity, file);
	fclose(file);
	if (size < 0) {
		free(pattern);
		return NULL;
	}
	*length = size;
	return pattern;
}

static void crash_with_test_vmas(void)
{
	struct rlimit limit = {
		.rlim_cur = RLIM_INFINITY,
		.rlim_max = RLIM_INFINITY,
	};
	unsigned int index;
	void *mapping;
	int fd;

	if (setrlimit(RLIMIT_CORE, &limit) ||
	    prctl(PR_SET_DUMPABLE, 1) ||
	    !write_file("/proc/self/coredump_filter", "0x19\n", 5))
		_exit(120);
	fd = open(FILE_TEST_PATH, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		_exit(121);
	mapping = mmap((void *)FILE_TEST_BASE, PROCESS_PAGE_SIZE, PROT_READ,
		       MAP_PRIVATE | MAP_FIXED_NOREPLACE, fd, PROCESS_PAGE_SIZE);
	close(fd);
	if (mapping != (void *)FILE_TEST_BASE)
		_exit(122);
	for (index = 0; index < TEST_VMAS; index++) {
		void *target = (void *)(TEST_BASE + index * TEST_STRIDE);

		mapping = mmap(target, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
			       -1, 0);
		if (mapping != target)
			_exit(123);
		memset(mapping, 0x40 + index, PROCESS_PAGE_SIZE);
	}
	if (driver_fd >= 0) {
		mapping = mmap((void *)DRIVER_TEST_BASE, 4 * PROCESS_PAGE_SIZE,
			       PROT_READ | PROT_WRITE,
			       MAP_SHARED | MAP_FIXED_NOREPLACE, driver_fd, 0);
		if (mapping != (void *)DRIVER_TEST_BASE)
			_exit(125);
	}
	crash_with_default_signal();
	_exit(124);
}

struct elf_note_header {
	uint32_t namesz;
	uint32_t descsz;
	uint32_t type;
};

static bool inspect_nt_file(int fd, uint64_t note_offset, uint64_t note_size,
			    bool *found,
			    uint64_t *page_size, uint64_t *file_offset)
{
	off_t end = note_offset + note_size;
	off_t pos = note_offset;

	while (pos + (off_t)sizeof(struct elf_note_header) <= end) {
		struct elf_note_header note;
		off_t desc_pos;
		off_t next;
		uint64_t *desc;
		uint64_t count;
		uint64_t index;

		if (pread(fd, &note, sizeof(note), pos) != sizeof(note))
			return false;
		desc_pos = (pos + sizeof(note) + note.namesz + 3) & ~3ULL;
		next = (desc_pos + note.descsz + 3) & ~3ULL;
		if (desc_pos < pos || next < desc_pos || next > end)
			return false;
		pos = next;
		if (note.type != NT_FILE || note.descsz < 2 * sizeof(*desc))
			continue;
		desc = malloc(note.descsz);
		if (!desc)
			return false;
		if (pread(fd, desc, note.descsz, desc_pos) != (ssize_t)note.descsz) {
			free(desc);
			return false;
		}
		count = desc[0];
		if (count > (note.descsz / sizeof(*desc) - 2) / 3) {
			free(desc);
			return false;
		}
		for (index = 0; index < count; index++) {
			uint64_t *entry = &desc[2 + 3 * index];

			if (entry[0] != FILE_TEST_BASE)
				continue;
			*found = true;
			*page_size = desc[1];
			*file_offset = entry[2];
			break;
		}
		free(desc);
	}
	return true;
}

static bool inspect_core(const char *path, unsigned int *found,
			 unsigned int *misaligned, bool *offset_segment_found,
			 uint64_t *offset_dump_size, bool *nt_file_found,
			 uint64_t *note_page_size, uint64_t *note_file_offset,
			 bool *contents_match)
{
	Elf64_Ehdr header;
	Elf64_Phdr phdr;
	unsigned int index;
	int fd;

	*found = 0;
	*misaligned = 0;
	*offset_segment_found = false;
	*offset_dump_size = 0;
	*nt_file_found = false;
	*note_page_size = 0;
	*note_file_offset = 0;
	*contents_match = true;
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	if (read(fd, &header, sizeof(header)) != sizeof(header) ||
	    memcmp(header.e_ident, ELFMAG, SELFMAG) ||
	    header.e_phentsize != sizeof(phdr))
		goto error;
	for (index = 0; index < header.e_phnum; index++) {
		unsigned long target_index;

		if (pread(fd, &phdr, sizeof(phdr),
			  header.e_phoff + index * sizeof(phdr)) != sizeof(phdr))
			goto error;
		if (phdr.p_type == PT_NOTE &&
		    !inspect_nt_file(fd, phdr.p_offset, phdr.p_filesz,
				     nt_file_found, note_page_size,
				     note_file_offset))
			goto error;
		if (phdr.p_type == PT_LOAD && phdr.p_vaddr == FILE_TEST_BASE) {
			*offset_segment_found = true;
			*offset_dump_size = phdr.p_filesz;
		}
		if (phdr.p_type != PT_LOAD || phdr.p_vaddr < TEST_BASE ||
		    phdr.p_vaddr >= TEST_BASE + TEST_VMAS * TEST_STRIDE)
			continue;
		target_index = (phdr.p_vaddr - TEST_BASE) / TEST_STRIDE;
		if (phdr.p_vaddr != TEST_BASE + target_index * TEST_STRIDE)
			continue;
		(*found)++;
		if (phdr.p_filesz < PROCESS_PAGE_SIZE) {
			*contents_match = false;
		} else {
			unsigned char contents[PROCESS_PAGE_SIZE];
			unsigned char expected = 0x40 + target_index;
			unsigned long byte;
			ssize_t bytes_read;

			bytes_read = pread(fd, contents, sizeof(contents),
					   phdr.p_offset);
			if (bytes_read != sizeof(contents)) {
				*contents_match = false;
			} else {
				for (byte = 0; byte < sizeof(contents); byte++) {
					if (contents[byte] != expected) {
						*contents_match = false;
						break;
					}
				}
			}
		}
		if (!phdr.p_align ||
		    phdr.p_offset % phdr.p_align != phdr.p_vaddr % phdr.p_align)
			(*misaligned)++;
	}
	close(fd);
	return true;

error:
	close(fd);
	return false;
}

static bool inspect_driver_core(const char *path)
{
	static const unsigned char expected[] = { 0xb4, 0x72, 0x31, 0x93 };
	unsigned char bytes[4 * PROCESS_PAGE_SIZE];
	Elf64_Ehdr header;
	Elf64_Phdr phdr;
	bool ok = false;
	unsigned int i;
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0)
		return false;
	if (pread(fd, &header, sizeof(header), 0) != sizeof(header))
		goto out;
	for (i = 0; i < header.e_phnum; i++) {
		size_t j;

		if (pread(fd, &phdr, sizeof(phdr),
			  header.e_phoff + i * sizeof(phdr)) != sizeof(phdr))
			break;
		if (phdr.p_type != PT_LOAD || phdr.p_vaddr != DRIVER_TEST_BASE)
			continue;
		if (phdr.p_filesz < sizeof(bytes) ||
		    pread(fd, bytes, sizeof(bytes), phdr.p_offset) != sizeof(bytes))
			break;
		ok = true;
		for (j = 0; j < sizeof(bytes); j++)
			ok &= bytes[j] == expected[j / PROCESS_PAGE_SIZE];
		break;
	}
out:
	close(fd);
	return ok;
}

static int run_test(void)
{
	char core_path[128];
	char *old_pattern;
	unsigned int found = 0;
	unsigned int misaligned = 0;
	uint64_t note_file_offset = 0;
	uint64_t note_page_size = 0;
	uint64_t offset_dump_size = 0;
	size_t old_pattern_length = 0;
	bool inspected = false;
	bool driver_ok = false;
	bool nt_file_found = false;
	bool offset_segment_found = false;
	bool contents_match = false;
	int status = 0;
	pid_t pid = -1;
	pid_t waited;

	ksft_print_header();
	ksft_set_plan(8);
	if (!create_offset_file())
		ksft_exit_fail_msg("create offset test file failed: %s\n",
				   strerror(errno));
	old_pattern = read_core_pattern(&old_pattern_length);
	if (!old_pattern) {
		unlink(FILE_TEST_PATH);
		ksft_exit_fail_msg("read core_pattern failed: %s\n",
				   strerror(errno));
	}
	if (!write_file("/proc/sys/kernel/core_pattern", CORE_PATTERN,
			strlen(CORE_PATTERN))) {
		free(old_pattern);
		unlink(FILE_TEST_PATH);
		ksft_exit_skip("cannot install a temporary core pattern\n");
	}
	ksft_test_result(true, "install a temporary core pattern\n");

	driver_fd = open("/dev/" IOV_ITER_PPPS_DEVICE_NAME, O_RDWR | O_CLOEXEC);
	pid = fork();
	if (!pid)
		crash_with_test_vmas();
	if (pid < 0) {
		ksft_test_result_fail("generate a core from the 4K process\n");
		ksft_test_result_fail("parse the generated ELF core\n");
		ksft_test_result_fail("keep 4K VMA offsets aligned in PT_LOAD headers\n");
		ksft_test_result_fail("preserve each 4K VMA's contents in PT_LOAD data\n");
		ksft_test_result_fail("do not dump a mapping that starts at file offset 4K\n");
		ksft_test_result_fail("encode the 4K file offset in NT_FILE\n");
		ksft_print_msg("fork failed: %s\n", strerror(errno));
		goto restore_pattern;
	}
	do {
		waited = waitpid(pid, &status, 0);
	} while (waited < 0 && errno == EINTR);
	if (waited != pid) {
		ksft_test_result_fail("generate a core from the 4K process\n");
		ksft_test_result_fail("parse the generated ELF core\n");
		ksft_test_result_fail("keep 4K VMA offsets aligned in PT_LOAD headers\n");
		ksft_test_result_fail("preserve each 4K VMA's contents in PT_LOAD data\n");
		ksft_test_result_fail("do not dump a mapping that starts at file offset 4K\n");
		ksft_test_result_fail("encode the 4K file offset in NT_FILE\n");
		ksft_print_msg("waitpid failed: %s\n", strerror(errno));
		goto remove_core;
	}
	ksft_test_result(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV,
			 "generate a core from the 4K process\n");

	snprintf(core_path, sizeof(core_path), "/tmp/elf-core-ppps-%d", pid);
	inspected = inspect_core(core_path, &found, &misaligned,
				 &offset_segment_found, &offset_dump_size,
				 &nt_file_found, &note_page_size,
				 &note_file_offset, &contents_match);
	ksft_test_result(inspected, "parse the generated ELF core\n");
	ksft_test_result(inspected && found == TEST_VMAS && !misaligned,
			 "keep 4K VMA offsets aligned in PT_LOAD headers\n");
	ksft_test_result(inspected && found == TEST_VMAS && contents_match,
			 "preserve each 4K VMA's contents in PT_LOAD data\n");
	ksft_test_result(inspected && offset_segment_found && !offset_dump_size,
			 "do not dump a mapping that starts at file offset 4K\n");
	ksft_test_result(inspected && nt_file_found &&
			 note_page_size == PROCESS_PAGE_SIZE && note_file_offset == 1,
			 "encode the 4K file offset in NT_FILE\n");
	if (driver_fd >= 0)
		driver_ok = inspected && inspect_driver_core(core_path);
	ksft_print_msg("VMAs=%u misaligned=%u contents=%u filesz=%llu page=%llu offset=%llu\n",
		       found, misaligned, contents_match,
		       (unsigned long long)offset_dump_size,
		       (unsigned long long)note_page_size,
		       (unsigned long long)note_file_offset);

remove_core:
	snprintf(core_path, sizeof(core_path), "/tmp/elf-core-ppps-%d", pid);
	unlink(core_path);
	unlink(FILE_TEST_PATH);

restore_pattern:
	if (driver_fd >= 0) {
		ksft_test_result(driver_ok, "dump initialized driver slices in virtual order\n");
		close(driver_fd);
	} else {
		ksft_test_result_skip("driver slice fixture unavailable\n");
	}
	unlink(FILE_TEST_PATH);
	if (!write_file("/proc/sys/kernel/core_pattern", old_pattern,
			old_pattern_length))
		ksft_print_msg("warning: could not restore core_pattern\n");
	free(old_pattern);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
