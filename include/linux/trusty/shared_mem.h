/*
 * Copyright (c) 2019 LK Trusty Authors. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define TRUSTY_MSG_BUF_SIZE (4096UL)
#define TRUSTY_MSG_MAX_PAGE_RUN_COUNT (TRUSTY_MSG_BUF_SIZE / 16 - 3)

/**
 * struct trusty_shared_mem_page_run - Address and size of page run.
 * phys_addr:   Physical address of start of page run.
 * size:        Size of page_run (in bytes).
 */
struct trusty_shared_mem_page_run {
    uint64_t phys_addr;
    uint64_t size;
};

/**
 * struct trusty_shared_mem_msg - Message to set up or tear down shared memory
 * id:                      Trusty assigned id used to identify shared memory
 *                          region. Must be 0 for the first %CMD_SHARE_MEM_ADD
 *                          message.
 * attr:                    Memory attributes (in arm64 pte format for arm and
 *                          arm64 systems).
 * pad:                     Reserved. Must be 0.
 * total_page_run_count:    Number of page_runs in shared memory region. Will be
 *                          greater than @page_run_count if larger than
 *                          %MAX_PAGE_RUN_COUNT.
 * page_run_start:          Number of page_runs passed in previous messages.
 * page_run_count:          Number of entries in @page_runs.
 * page_runs:               Array of physical address and size of physically
 *                          contiguous memory regions.
 */
struct trusty_shared_mem_msg {
    uint64_t id;
    uint64_t attr;
    uint64_t pad;
    uint64_t total_page_run_count;
    uint64_t page_run_start;
    uint64_t page_run_count;
    struct trusty_shared_mem_page_run page_runs[TRUSTY_MSG_MAX_PAGE_RUN_COUNT];
};
