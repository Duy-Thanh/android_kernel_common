.. SPDX-License-Identifier: GPL-2.0

get_freqtbl support for arm/arm64
=============================

Allows guest to query for host's frequency table information.

* ARM_SMCCC_VENDOR_HYP_KVM_GET_CPUFREQ_TBL_FUNC_ID: 0x86000042

This hypercall uses the SMC32/HVC32 calling convention:

ARM_SMCCC_VENDOR_HYP_KVM_GET_CPUFREQ_TBL_FUNC_ID
    ==============    ========    =====================================
    Function ID:      (uint32)    0x86000042
    Arguments:        (uint32)    index of the affined CPU's freq table
    Return Values:    (int32)     NOT_SUPPORTED(-1) on error, or
                      (uint32)    Frequency of requested index of affined CPU(r1)
    Endianness:                   No Restrictions.
    ==============    ========    =====================================
