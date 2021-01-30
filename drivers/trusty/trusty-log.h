/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2015 Google, Inc.
 *
 * Trusty also has a copy of this header.  Please keep the copies in sync.
 */
#ifndef _TRUSTY_LOG_H_
#define _TRUSTY_LOG_H_

/*
 * Below is the log-data-entry in Ringbuffer
 * {
 *    ----- header part -----
 *    total_size_of_entry +
 *    log_string + app_name +
 *    ------footer part -----
 *    log-level +
 *    app-id +
 *    timestamp +
 *    app_name_size +
 *    log_string_size
 *    -----------------------
 * }
 */
struct log_data_header {
	uint32_t entry_size;
	char data[192];
} __attribute__((packed));

struct log_data_footer {
	uint32_t log_level;
	int32_t app_id;
	uint64_t timestamp;
	uint32_t app_name_len;
	uint32_t log_len;
} __attribute__((packed));

/*
 * Ring buffer that supports one secure producer thread and one
 * linux side consumer thread.
 */
struct log_rb {
	volatile uint32_t alloc;
	volatile uint32_t put;
	uint32_t sz;
	volatile char data[];
} __packed;

#define SMC_SC_SHARED_LOG_VERSION	SMC_STDCALL_NR(SMC_ENTITY_LOGGING, 0)
#define SMC_SC_SHARED_LOG_ADD		SMC_STDCALL_NR(SMC_ENTITY_LOGGING, 1)
#define SMC_SC_SHARED_LOG_RM		SMC_STDCALL_NR(SMC_ENTITY_LOGGING, 2)

#define TRUSTY_LOG_API_VERSION	2

#endif
