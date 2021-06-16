// SPDX-License-Identifier: GPL-2.0
/**
 * trace.c - DesignWare USB3 DRD Controller Trace Support
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - https://www.ti.com
 *
 * Author: Felipe Balbi <balbi@ti.com>
 */

#define CREATE_TRACE_POINTS
#include "trace.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(dwc3_ep_queue);
EXPORT_TRACEPOINT_SYMBOL_GPL(dwc3_readl);
EXPORT_TRACEPOINT_SYMBOL_GPL(dwc3_writel);
<<<<<<< HEAD   (a34f0a Merge 5.10.44 into android13-5.10)
=======
EXPORT_TRACEPOINT_SYMBOL_GPL(dwc3_event);
EXPORT_TRACEPOINT_SYMBOL_GPL(dwc3_ctrl_req);
EXPORT_TRACEPOINT_SYMBOL_GPL(dwc3_complete_trb);
>>>>>>> BRANCH (fbf19a gki_config: Disable CONFIG_DEBUG_STACK_USAGE)
