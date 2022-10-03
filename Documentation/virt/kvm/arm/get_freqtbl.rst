.. SPDX-License-Identifier: GPL-2.0

getfreq support for arm/arm64
=============================

Getfreq support to share frequency between host to guest.

* ARM_SMCCC_VENDOR_HYP_KVM_GET_CPUFREQ_TBL_FUNC_ID: 0x86000004

This hypercall uses the SMC32/HVC32 calling convention:

ARM_SMCCC_VENDOR_HYP_KVM_GET_CPUFREQ_TBL_FUNC_ID
    ==============    ========    =====================================
    Function ID:      (uint32)    0x86000004
    Arguments:        (uint32)    index of the affined CPU's freq table
    Return Values:    (int32)     NOT_SUPPORTED(-1) on error, or
                      (uint32)    Frequency of requested index of affined CPU(r1)
    Endianness:                   No Restrictions.
    ==============    ========    =====================================
