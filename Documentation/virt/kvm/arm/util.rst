.. SPDX-License-Identifier: GPL-2.0

Util_sync support for arm64
============================

Util_sync is used for sharing the utilization value between host and
guests.

* ARM_SMCCC_HYP_KVM_UTIL_FUNC_ID: 0x86000003

This hypercall using the SMC32/HVC32 calling convention:

ARM_SMCCC_HYP_KVM_UTIL_FUNC_ID
    ==============    =========   ============================
    Function ID:      (uint32)    0x86000003
    Arguments:        (uint32)    uclamp min value(0-1024)
                      (uint32)    uclamp max value(0-1024)
    Return values:    (int32)     NOT_SUPPORTED(-1) on error.
    Endianness:                   Must be the same endianness
                                  to the host.
    ==============    ========    ============================
