/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Google, Inc.
 */

#include <linux/bits.h>

#include "smcall.h"

#define SMC_ENTITY_SHARED_MEMORY    62

#define SMC_FASTCALL_NR_SHARED_MEMORY(nr) \
	SMC_FASTCALL_NR(SMC_ENTITY_SHARED_MEMORY, nr)
#define SMC_FASTCALL64_NR_SHARED_MEMORY(nr) \
	SMC_FASTCALL64_NR(SMC_ENTITY_SHARED_MEMORY, nr)

struct spci_constituent_memory_region_descriptor {
	u64 address;
	u32 page_count;
} __packed;

#define SPCI_MEM_ATTR_RW BIT(6)
#define SPCI_MEM_ATTR_NORMAL_MEMORY_CACHED_WB (0x7U << 2)
#define SPCI_MEM_ATTR_INNER_SHAREABLE (0x3U << 0)

struct spci_memory_region_attributes_descriptor {
	u16 receiver_id;
	u16 memory_attributes;
};

struct spci_memory_region_descriptor {
	u32 tag;
	u32 flags;
	u16 sender_id;
	u16 reserved;
	u32 total_page_count;
	u32 constituent_memory_region_count;
	u32 constituent_memory_region_descriptor_offset;
	u32 memory_region_attributes_descriptor_count;
	struct spci_memory_region_attributes_descriptor
		memory_region_attributes_descriptors[];
};

struct spci_memory_retrieve_descriptor {
	u32 handle;
	u16 sender_id;
	u16 reserved;
	u32 transaction_type;
	u32 tag;
	u32 global_memory_region_attributes_descriptors_count;
	u32 global_memory_region_attributes_descriptors_offset;
	u32 retrieve_properties_descriptor_count;
	struct spci_memory_region_attributes_descriptor
		retrieve_properties_descriptors[];
};

#define SPCI_ERROR_NOT_SUPPORTED        (-1)
#define SPCI_ERROR_INVALID_PARAMETERS   (-2)
#define SPCI_ERROR_NO_MEMORY            (-3)
#define SPCI_ERROR_DENIED               (-6)

/**
 * SMC_FC_SPCI_ERROR
 * @w1:     VMID in [31:16], vCPU in [15:0]
 * @w2:     Error code (SPCI_ERROR_*)
 */
#define SMC_FC_SPCI_ERROR SMC_FASTCALL_NR_SHARED_MEMORY(0x60)

/**
 * SMC_FC_SPCI_SUCCESS/SMC_FC64_SPCI_SUCCESS
 * @w1:             VMID in [31:16], vCPU in [15:0]
 * @w2/x2-w2-w7:    Function specific
 */
#define SMC_FC_SPCI_SUCCESS SMC_FASTCALL_NR_SHARED_MEMORY(0x61)
#define SMC_FC64_SPCI_SUCCESS SMC_FASTCALL64_NR_SHARED_MEMORY(0x61)

/**
 * SMC_FC_SPCI_RXTX_MAP/SMC_FC64_SPCI_RXTX_MAP
 * @w1/x1:  TX address
 * @w2/x2:  RX address
 * @w3/x3:  RX/TX page count
 *
 * Return: SMC_FC_SPCI_SUCCESS
 */
#define SMC_FC_SPCI_RXTX_MAP SMC_FASTCALL_NR_SHARED_MEMORY(0x66)
#define SMC_FC64_SPCI_RXTX_MAP SMC_FASTCALL64_NR_SHARED_MEMORY(0x66)

/**
 * SMC_FC_SPCI_RXTX_UNMAP
 * @w1:     ID in [31:16]
 *
 * Return: SMC_FC_SPCI_SUCCESS
 */
#define SMC_FC_SPCI_RXTX_UNMAP SMC_FASTCALL_NR_SHARED_MEMORY(0x67)

/**
 * SMC_FC_SPCI_ID_GET
 *
 * Return: SMC_FC_SPCI_SUCCESS, 0, ID
 */
#define SMC_FC_SPCI_ID_GET SMC_FASTCALL_NR_SHARED_MEMORY(0x69)

/**
 * SMC_FC_SPCI_MEM_SHARE/SMC_FC64_SPCI_MEM_SHARE
 * @w1/x1:  Base address
 * @w2:     Page count
 * @w3:     Fragment length
 * @w4:     Length
 * @w5:     Cookie
 *
 * Return: SMC_FC_SPCI_SUCCESS, 0, Handle
 */
#define SMC_FC_SPCI_MEM_SHARE SMC_FASTCALL_NR_SHARED_MEMORY(0x73)
#define SMC_FC64_SPCI_MEM_SHARE SMC_FASTCALL64_NR_SHARED_MEMORY(0x73)

/**
 * SMC_FC_SPCI_MEM_RETRIEVE_REQ/SMC_FC64_SPCI_MEM_RETRIEVE_REQ
 * @w1/x1:  Base address
 * @w2:     Page count
 * @w3:     Fragment length
 * @w4:     Length
 * @w5:     Handle
 */
#define SMC_FC_SPCI_MEM_RETRIEVE_REQ SMC_FASTCALL_NR_SHARED_MEMORY(0x74)
#define SMC_FC64_SPCI_MEM_RETRIEVE_REQ SMC_FASTCALL64_NR_SHARED_MEMORY(0x74)

/**
 * SMC_FC_SPCI_MEM_RETRIEVE_RESP/SMC_FC64_SPCI_MEM_RETRIEVE_RESP
 * @w1/x1:  0
 * @w2:     0
 * @w3:     Fragment length
 * @w4:     Length
 * @w5:     Handle
 */
#define SMC_FC_SPCI_MEM_RETRIEVE_RESP SMC_FASTCALL_NR_SHARED_MEMORY(0x75)
#define SMC_FC64_SPCI_MEM_RETRIEVE_RESP SMC_FASTCALL64_NR_SHARED_MEMORY(0x75)

/**
 * SMC_FC_SPCI_MEM_RELINQUISH
 */
#define SMC_FC_SPCI_MEM_RELINQUISH SMC_FASTCALL_NR_SHARED_MEMORY(0x76)

/**
 * SMC_FC_SPCI_MEM_RECLAIM
 * @w1:     Handle
 * @w2:     Flags
 */
#define SMC_FC_SPCI_MEM_RECLAIM SMC_FASTCALL_NR_SHARED_MEMORY(0x77)
