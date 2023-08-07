# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2022 The Android Open Source Project

"""
This module contains a full list of kernel modules
 compiled by GKI.
"""

_COMMON_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/block/zram/zram.ko",
    "mm/zsmalloc.ko",
]

# Deprecated - Use `get_gki_modules_list` function instead.
COMMON_GKI_MODULES_LIST = _COMMON_GKI_MODULES_LIST

_ARM64_GKI_MODULES_LIST = [
    # keep sorted
]

_RISCV64_GKI_MODULES_LIST = [
    # keep sorted
]

_X86_64_GKI_MODULES_LIST = [
    # keep sorted
]

# buildifier: disable=unnamed-macro
def get_gki_modules_list(arch = None):
    """ Provides the list of GKI modules.

    Args:
      arch: One of [arm64, x86_64, riscv64].

    Returns:
      The list of GKI modules for the given |arch|.
    """
    gki_modules_list = [] + _COMMON_GKI_MODULES_LIST
    if arch == "arm64":
        gki_modules_list += _ARM64_GKI_MODULES_LIST
    elif arch == "x86_64":
        gki_modules_list += _X86_64_GKI_MODULES_LIST
    elif arch == "riscv64":
        gki_modules_list += _RISCV64_GKI_MODULES_LIST
    else:
        fail("{}: arch {} not supported. Use one of [arm64, x86_64, riscv64]".format(
            str(native.package_relative_label(":x")).removesuffix(":x"),
            arch,
        ))

    return gki_modules_list
