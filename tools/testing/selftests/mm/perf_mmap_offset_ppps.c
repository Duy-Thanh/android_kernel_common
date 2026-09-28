// SPDX-License-Identifier: GPL-2.0
/*
 * perf ring buffers of a 4K compat process are laid out, bounds-checked and
 * wrapped in process-page units, and PERF_RECORD_MMAP reports the 4K file
 * slice offset of a compat mapping.
 */
#define _GNU_SOURCE

#include <setjmp.h>
#include <linux/memfd.h>
#include <linux/perf_event.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

#define FILE_SIZE	(4 * PROCESS_PAGE_SIZE)
#define FILE_OFFSET	PROCESS_PAGE_SIZE
#define DATA_PAGES	256

static sigjmp_buf write_fault_jmp;

static void write_fault_handler(int signal)
{
	(void)signal;
	siglongjmp(write_fault_jmp, 1);
}

struct mmap_record {
	struct perf_event_header header;
	uint32_t pid;
	uint32_t tid;
	uint64_t addr;
	uint64_t len;
	uint64_t pgoff;
	char filename[];
};

static int perf_event_open(struct perf_event_attr *attr)
{
	return syscall(__NR_perf_event_open, attr, 0, -1, -1, 0);
}

static void copy_ring(void *destination, const unsigned char *data,
		      size_t data_size, uint64_t position, size_t length)
{
	size_t offset = position % data_size;
	size_t first = data_size - offset;

	if (first > length)
		first = length;
	memcpy(destination, data + offset, first);
	if (first < length)
		memcpy((unsigned char *)destination + first, data,
		       length - first);
}

static bool find_mmap_offset(struct perf_event_mmap_page *metadata,
			     size_t page_size, uint64_t *file_offset)
{
	size_t data_offset = metadata->data_offset ?: page_size;
	size_t data_size = metadata->data_size ?: DATA_PAGES * page_size;
	const unsigned char *data = (const unsigned char *)metadata + data_offset;
	uint64_t head = __atomic_load_n(&metadata->data_head, __ATOMIC_ACQUIRE);
	uint64_t position = metadata->data_tail;
	bool found = false;

	while (position < head) {
		struct perf_event_header header;
		struct mmap_record *record;

		copy_ring(&header, data, data_size, position, sizeof(header));
		if (header.size < sizeof(header) || header.size > data_size)
			break;
		record = malloc(header.size);
		if (!record)
			break;
		copy_ring(record, data, data_size, position, header.size);
		if (record->header.type == PERF_RECORD_MMAP &&
		    record->header.size > sizeof(*record) &&
		    strstr(record->filename, "perf-mmap-offset-ppps")) {
			*file_offset = record->pgoff;
			found = true;
		}
		position += header.size;
		free(record);
		if (found)
			break;
	}
	__atomic_store_n(&metadata->data_tail, position, __ATOMIC_RELEASE);
	return found;
}

static int open_perf_ring(struct perf_event_attr *attr,
			  struct perf_event_mmap_page **metadata,
			  size_t data_pages, size_t *page_size,
			  size_t *ring_size)
{
	int fd = perf_event_open(attr);

	if (fd < 0)
		return -1;
	*page_size = PROCESS_PAGE_SIZE;
	*ring_size = (data_pages + 1) * *page_size;
	*metadata = mmap(NULL, *ring_size, PROT_READ | PROT_WRITE,
			 MAP_SHARED, fd, 0);
	if (*metadata != MAP_FAILED)
		return fd;
	close(fd);
	return -1;
}

static bool data_ring_is_readonly(struct perf_event_mmap_page *metadata)
{
	struct sigaction action = {
		.sa_handler = write_fault_handler,
	};
	struct sigaction old_bus;
	struct sigaction old_segv;
	unsigned char *data = (unsigned char *)metadata +
		metadata->data_offset;
	bool faulted;

	sigemptyset(&action.sa_mask);
	if (sigaction(SIGBUS, &action, &old_bus))
		return false;
	if (sigaction(SIGSEGV, &action, &old_segv)) {
		sigaction(SIGBUS, &old_bus, NULL);
		return false;
	}
	if (sigsetjmp(write_fault_jmp, 1)) {
		faulted = true;
	} else {
		*data = 0x5a;
		faulted = false;
	}
	sigaction(SIGBUS, &old_bus, NULL);
	sigaction(SIGSEGV, &old_segv, NULL);
	return faulted;
}

static bool exercise_small_ring(struct perf_event_attr *attr)
{
	struct perf_event_mmap_page *metadata = MAP_FAILED;
	uint64_t recorded_offset;
	size_t ring_size = 0;
	size_t page_size = 0;
	bool success = true;
	int perf_fd;
	int i;

	perf_fd = open_perf_ring(attr, &metadata, 1, &page_size, &ring_size);
	if (perf_fd < 0 || metadata == MAP_FAILED)
		return false;
	if (metadata->data_offset != page_size ||
	    metadata->data_size != page_size ||
	    ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, 0)) {
		success = false;
		goto out;
	}
	for (i = 0; i < 128; i++) {
		void *mapping;
		int memfd;

		memfd = memfd_create("perf-mmap-offset-ppps-small", MFD_CLOEXEC);
		if (memfd < 0 || ftruncate(memfd, PROCESS_PAGE_SIZE)) {
			if (memfd >= 0)
				close(memfd);
			success = false;
			break;
		}
		mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_EXEC,
			       MAP_PRIVATE, memfd, 0);
		if (mapping == MAP_FAILED ||
		    !find_mmap_offset(metadata, page_size, &recorded_offset))
			success = false;
		if (mapping != MAP_FAILED)
			munmap(mapping, PROCESS_PAGE_SIZE);
		close(memfd);
		if (!success)
			break;
	}
	ioctl(perf_fd, PERF_EVENT_IOC_DISABLE, 0);
	success = success && metadata->data_head > 2 * metadata->data_size;
out:
	munmap(metadata, ring_size);
	close(perf_fd);
	return success;
}

static bool map_metadata_only(struct perf_event_attr *attr)
{
	struct perf_event_mmap_page *metadata;
	int fd;

	fd = perf_event_open(attr);
	if (fd < 0)
		return false;
	metadata = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
	if (metadata == MAP_FAILED) {
		close(fd);
		return false;
	}
	munmap(metadata, PROCESS_PAGE_SIZE);
	close(fd);
	return true;
}

static bool reject_subpage_ring_offset(struct perf_event_attr *attr,
				       int *saved_errno)
{
	void *mapping;
	int fd;

	fd = perf_event_open(attr);
	if (fd < 0)
		return false;
	errno = 0;
	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, PROCESS_PAGE_SIZE);
	*saved_errno = errno;
	if (mapping != MAP_FAILED)
		munmap(mapping, PROCESS_PAGE_SIZE);
	close(fd);
	return mapping == MAP_FAILED;
}

static int run_test(void)
{
	struct perf_event_attr attr = {
		.type = PERF_TYPE_SOFTWARE,
		.size = sizeof(attr),
		.config = PERF_COUNT_SW_DUMMY,
		.disabled = 1,
		.mmap = 1,
		.mmap_data = 1,
	};
	struct perf_event_mmap_page *metadata = MAP_FAILED;
	uint64_t recorded_offset = UINT64_MAX;
	size_t page_size = 0;
	size_t ring_size = 0;
	size_t data_offset;
	size_t data_size;
	void *file_mapping;
	bool found;
	int offset_errno = 0;
	int memfd;
	int perf_fd;

	ksft_print_header();
	ksft_set_plan(10);
	ksft_test_result(map_metadata_only(&attr),
			 "map a metadata-only perf event with one process page\n");
	ksft_test_result(reject_subpage_ring_offset(&attr, &offset_errno),
			 "reject a perf ring mmap at subpage offset 4K (errno=%d)\n",
			 offset_errno);

	perf_fd = open_perf_ring(&attr, &metadata, DATA_PAGES, &page_size,
				 &ring_size);
	ksft_test_result(page_size && perf_fd >= 0 && metadata != MAP_FAILED,
			 "open and map a software perf event\n");
	if (!page_size || perf_fd < 0 || metadata == MAP_FAILED)
		ksft_exit_fail_msg("perf event setup failed: %s\n",
				   strerror(errno));
	data_offset = metadata->data_offset ?: page_size;
	data_size = metadata->data_size ?: DATA_PAGES * page_size;
	ksft_print_msg("ring_size=%zu data_offset=%zu data_size=%zu\n",
		       ring_size, data_offset, data_size);
	ksft_test_result(data_offset <= ring_size &&
			 data_size <= ring_size - data_offset,
			 "keep the effective perf data ring inside the VMA\n");
	ksft_test_result(data_offset == page_size &&
			 data_size == DATA_PAGES * page_size,
			 "preserve the requested perf data ring capacity\n");
	ksft_test_result(data_ring_is_readonly(metadata),
			 "keep perf data pages read-only\n");
	if (ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, 0))
		ksft_exit_fail_msg("perf enable failed: %s\n", strerror(errno));

	memfd = memfd_create("perf-mmap-offset-ppps", MFD_CLOEXEC);
	if (memfd < 0 || ftruncate(memfd, FILE_SIZE))
		ksft_exit_fail_msg("memfd setup failed: %s\n", strerror(errno));
	file_mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_EXEC,
			    MAP_PRIVATE, memfd, FILE_OFFSET);
	ksft_test_result(file_mapping != MAP_FAILED,
			 "map one 4K page at file offset 4K\n");
	if (file_mapping == MAP_FAILED)
		ksft_exit_fail_msg("file mmap failed: %s\n", strerror(errno));

	ioctl(perf_fd, PERF_EVENT_IOC_DISABLE, 0);
	found = find_mmap_offset(metadata, page_size, &recorded_offset);
	ksft_test_result(found, "find the target PERF_RECORD_MMAP\n");
	ksft_print_msg("recorded pgoff=%#llx expected=%#lx\n",
		       (unsigned long long)recorded_offset, FILE_OFFSET);
	ksft_test_result(found && recorded_offset == FILE_OFFSET,
			 "perf reports the PPPS file slice offset\n");
	ksft_test_result(exercise_small_ring(&attr),
			 "wrap a one-process-page perf data ring\n");

	munmap(file_mapping, PROCESS_PAGE_SIZE);
	close(memfd);
	munmap(metadata, ring_size);
	close(perf_fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
