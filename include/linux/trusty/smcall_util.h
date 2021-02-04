/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Google, Inc.
 */

#ifndef __LINUX_TRUSTY_SMCALL_UTIL_H
#define __LINUX_TRUSTY_SMCALL_UTIL_H

static inline const char* smc_name(u32 smcnr) {
    switch (smcnr) {
        case SMC_SC_RESTART_LAST:
            return "SMC_SC_RESTART_LAST";
        case SMC_SC_LOCKED_NOP:
            return "SMC_SC_LOCKED_NOP";
        case SMC_SC_RESTART_FIQ:
            return "SMC_SC_RESTART_FIQ";
        case SMC_SC_NOP:
            return "SMC_SC_NOP";
        case SMC_FC_RESERVED:
            return "SMC_FC_RESERVED";
        case SMC_FC_FIQ_EXIT:
            return "SMC_FC_FIQ_EXIT";
        case SMC_FC_REQUEST_FIQ:
            return "SMC_FC_REQUEST_FIQ";
        case SMC_FC_GET_NEXT_IRQ:
            return "SMC_FC_GET_NEXT_IRQ";
        case SMC_FC_CPU_SUSPEND:
            return "SMC_FC_CPU_SUSPEND";
        case SMC_FC_CPU_RESUME:
            return "SMC_FC_CPU_RESUME";
        case SMC_FC_AARCH_SWITCH:
            return "SMC_FC_AARCH_SWITCH";
        case SMC_FC_GET_VERSION_STR:
            return "SMC_FC_GET_VERSION_STR";
        case SMC_FC_API_VERSION:
            return "SMC_FC_API_VERSION";
        case SMC_SC_VIRTIO_GET_DESCR:
            return "SMC_SC_VIRTIO_GET_DESCR";
        case SMC_SC_VIRTIO_START:
            return "SMC_SC_VIRTIO_START";
        case SMC_SC_VIRTIO_STOP:
            return "SMC_SC_VIRTIO_STOP";
        case SMC_SC_VDEV_RESET:
            return "SMC_SC_VDEV_RESET";
        case SMC_SC_VDEV_KICK_VQ:
            return "SMC_SC_VDEV_KICK_VQ";
        case SMC_NC_VDEV_KICK_VQ:
            return "SMC_NC_VDEV_KICK_VQ";
        default:
            return "SMC_unknown";
    }
}

#endif /* __LINUX_TRUSTY_SMCALL_UTIL_H */
