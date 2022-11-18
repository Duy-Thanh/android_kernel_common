/* SPDX-License-Identifier: GPL-2.0 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/sysmacros.h>

#include <lkl.h>
#include <lkl_host.h>

#include "../../lib/virtio_pci.h"

#define unlikely(x) __builtin_expect(!!(x), 0)

#define LOG(fmt, ...)                                                          \
	if (g_log_enabled) {                                                   \
		printf(fmt, ##__VA_ARGS__);                                    \
	}

#define PCI_VENDOR_ID_REDHAT_QUMRANET 0x1af4

static bool g_log_enabled = true;

static int g_fuzz_iterations = 0;

static int lkl_init()
{
	if (!g_log_enabled) {
		lkl_host_ops.print = NULL;
	}

	int ret = lkl_start_kernel(
		&lkl_host_ops,
		"mem=50M loglevel=8 lkl_pci=virtio_pci_fuzzing_dev");
	if (ret) {
		LOG("lkl_start_kernel failed\n");
		return -1;
	}

	lkl_mount_fs("sysfs");
	lkl_mount_fs("proc");
	lkl_mount_fs("dev");

	return 0;
}

void __llvm_profile_initialize_file(void);
int __llvm_profile_write_file(void);

void flush_coverage()
{
	LOG("Flushing coverage data...\n");
	__llvm_profile_write_file();
	LOG("Done...\n");
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	for (int i = 0; i < *argc; i++) {
		if (strcmp((*argv)[i], "-quiet=1") == 0) {
			g_log_enabled = false;
			break;
		}
	}

	lkl_init();

	__llvm_profile_initialize_file();
	atexit(flush_coverage);
	return 0;
}

struct fuzz_data {
	uint16_t number_of_virtio_queues;
	uint16_t virtio_queue_size;
	struct pci_config pci_config;
	// Virtio device configuration is device-specific and,
	// thus, may vary between different drivers. The value
	// below seems to be reasonable to cover most of the devices.
	uint8_t virtio_dev_config[128];
};

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	LOG("Begin fuzz iteration.\n");

	g_fuzz_iterations++;
	if (unlikely(g_fuzz_iterations > 1000)) {
		flush_coverage();
		g_fuzz_iterations = 0;
	}

	struct fuzz_data fuzz;
	memset(&fuzz, 0x00, sizeof(fuzz));
	memcpy(&fuzz, Data, Size > sizeof(fuzz) ? sizeof(fuzz) : Size);

	struct virtio_pci_dev pci_dev;
	memset(&pci_dev, 0x00, sizeof(pci_dev));

	memcpy(&pci_dev.config.common_config, &fuzz.pci_config,
	       sizeof(fuzz.pci_config));

	// If we don't provide PCI VID/PID matching any of the enabled PCI device
	// drivers (in this case it is only virtio_pci) then the fuzzer may hang.
	pci_dev.config.virtio_pci_config.hdr.vendor_id =
		PCI_VENDOR_ID_REDHAT_QUMRANET;

	// Set non zero value to prevent probing PCI legacy driver as it's
	// currently not supported by the fuzzer.
	pci_dev.config.virtio_pci_config.hdr.revision_id = 1;

	assert(virtio_pci_dev_add(&pci_dev, fuzz.number_of_virtio_queues,
				  fuzz.virtio_queue_size,
				  fuzz.virtio_dev_config,
				  sizeof(fuzz.virtio_dev_config)) == 0);
	assert(virtio_pci_dev_remove(&pci_dev) == 0);

	LOG("Done.\n");

	return 0;
}
