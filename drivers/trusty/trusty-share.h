/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2022 Google, Inc.
 *
 * This header file defines the SMC API and the shared data info between
 * Linux and Trusty.
 *
 * Important: Copy of this header file is used in Trusty.
 * Trusty header file:
 *   trusty/trusty/kernel/lib/trusty/include/lib/trusty/trusty_share.h
 * Please keep the copies in sync.
 */
#ifndef _TRUSTY_SHARE_H_
#define _TRUSTY_SHARE_H_

#include <linux/trusty/smcall.h>

/*
 * trusty-shadow-priority valid values
 */
#define TRUSTY_SHADOW_PRIORITY_LOW 1
#define TRUSTY_SHADOW_PRIORITY_NORMAL 2
#define TRUSTY_SHADOW_PRIORITY_HIGH 3

/**
 * struct trusty_shadow_priority - per-cpu trusty shadow-priority
 * @cur_shadow_priority: set by Trusty-Driver/Linux
 * @ask_shadow_priority: set by Trusty Kernel
 */
struct trusty_shadow_priority {
	u32 cur_shadow_priority;
	u32 ask_shadow_priority;
};

/**
 * struct trusty_shared - information in the shared memory.
 * @cpu_count: max number of available CPUs in the system.
 * @trusty_info_table: table containing per-cpu trusty-info.
 *                     table in indexed by cpu_id.
 */
struct trusty_shared {
	u32 cpu_count;
	struct trusty_shadow_priority trusty_shadow_priority_table[];
};

/*
 * SMC API for supporting trusty-shadow-priority
 */
#define SMC_SC_SHARE_REGISTER SMC_STDCALL_NR(SMC_ENTITY_SECURE_MONITOR, 4)
#define SMC_SC_SHARE_UNREGISTER SMC_STDCALL_NR(SMC_ENTITY_SECURE_MONITOR, 5)

#endif /* _TRUSTY_SHARE_H_ */
