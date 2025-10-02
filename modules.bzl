# SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
# Copyright (C) 2025 The Android Open Source Project

"""Re-exports of symbols for external usage regarding to lists of modules.

DO NOT ADD MORE SYMBOLS. Additional symbols for external usage should be added
to bazel/modules.bzl, NOT modules.bzl (this file) or bazel/modules_private.bzl.
"""

load(
    ":bazel/modules.bzl",
    _get_gki_modules_list = "get_gki_modules_list",
    _get_kunit_modules_list = "get_kunit_modules_list",
)

visibility("public")

<<<<<<< HEAD   (2c5561898d98f3f96cecd2c77df538b1c7da1b5d ANDROID: power_supply: Use fwnode to retrieve psy array from)
_ARM_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/ptp/ptp_kvm.ko",
]

_ARM64_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/char/hw_random/cctrng.ko",
    "drivers/misc/open-dice.ko",
    "drivers/ptp/ptp_kvm.ko",
]

_X86_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/ptp/ptp_kvm.ko",
]

_X86_64_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/acpi/fan.ko",
    "drivers/powercap/intel_rapl_common.ko",
    "drivers/ptp/ptp_kvm.ko",
    "drivers/thermal/intel/int340x_thermal/acpi_thermal_rel.ko",
    "drivers/thermal/intel/int340x_thermal/int3400_thermal.ko",
    "drivers/thermal/intel/int340x_thermal/int3401_thermal.ko",
    "drivers/thermal/intel/int340x_thermal/int3402_thermal.ko",
    "drivers/thermal/intel/int340x_thermal/int3403_thermal.ko",
    "drivers/thermal/intel/int340x_thermal/int340x_thermal_zone.ko",
    "drivers/thermal/intel/int340x_thermal/platform_temperature_control.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_device.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_device_pci.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_device_pci_legacy.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_mbox.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_power_floor.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_rapl.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_rfim.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_wt_hint.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_wt_req.ko",
    "drivers/thermal/intel/intel_soc_dts_iosf.ko",
    "drivers/thermal/intel/intel_soc_dts_thermal.ko",
]

def _apply(map_each, lst):
    if not map_each:
        return lst
    ret = []
    for elem in lst:
        mapped = map_each(elem)
        if mapped:
            ret.append(mapped)
    return ret

def _get_gki_modules_list_minus_select(arch, map_each):
    """ Provides the list of GKI modules, minus those in select() branches.

    Args:
        arch: One of [arm, arm64, i386, x86_64].
        map_each: A function that takes the module name as parameter, and returns
            the mapped value. If the module should be filtered out, the function
            should return None.

    Returns:
        The list of GKI modules for the given |arch|.
    """
    if not arch in ("arm64", "x86_64", "arm", "i386"):
        fail("{}: arch {} not supported. Use one of [arm, arm64, i386, x86_64]".format(
            str(native.package_relative_label(":x")).removesuffix(":x"),
            arch,
        ))

    if arch == "arm":
        return _apply(map_each, _COMMON_GKI_MODULES_LIST + _ARM_GKI_MODULES_LIST)

    if arch == "i386":
        return _apply(map_each, _COMMON_GKI_MODULES_LIST + _X86_GKI_MODULES_LIST)

    gki_modules_list = _apply(map_each, [] + _COMMON_GKI_MODULES_LIST)
    if arch == "arm64":
        gki_modules_list += _apply(map_each, _ARM64_GKI_MODULES_LIST)
    elif arch == "x86_64":
        gki_modules_list += _apply(map_each, _X86_64_GKI_MODULES_LIST)

    return gki_modules_list

# buildifier: disable=unnamed-macro
def get_gki_modules_list(arch = None, map_each = None):
    """Provides the list of GKI modules.

    Args:
        arch: One of [arm, arm64, i386, x86_64].
        map_each: A function that takes the module name as parameter, and
            returns the mapped value. If the module should be filtered out, the
            function should return None.

    Returns:
        An opaque expression that represents the list of GKI modules for the
        given |arch|. Do not treat the returned value as a list (e.g. use
        list comprehension); instead, use the |map_each| argument.
    """

    return select({
        "//conditions:default": _get_gki_modules_list_minus_select(arch, map_each),
    })

# buildifier: disable=unnamed-macro
def get_gki_modules_superset(arch = None, map_each = None):
    """Provides the list of superset of GKI modules.

    This includes all modules on each branch of the conditionals. For example,
    Rust modules may always be included regardless of the value of
    --kasan_sw_tags.

    Args:
        arch: One of [arm, arm64, i386, x86_64].
        map_each: A function that takes the module name as parameter, and
            returns the mapped value. If the module should be filtered out, the
            function should return None.

    Returns:
        A list that contains the superset of GKI modules for the given |arch|.
    """
    return _get_gki_modules_list_minus_select(arch, map_each)

_KUNIT_FRAMEWORK_MODULES = [
    "lib/kunit/kunit.ko",
]

# Modules defined by tools/testing/kunit/configs/android/kunit_defconfig
_KUNIT_COMMON_MODULES_LIST = [
    # keep sorted
    "drivers/android/tests/binder_alloc_kunit.ko",
    "drivers/base/regmap/regmap-kunit.ko",
    "drivers/base/regmap/regmap-ram.ko",
    "drivers/base/regmap/regmap-raw-ram.ko",
    "drivers/hid/hid-uclogic-test.ko",
    "drivers/iio/test/iio-test-format.ko",
    "drivers/input/tests/input_test.ko",
    "drivers/of/of_kunit_helpers.ko",
    "drivers/rtc/test_rtc_lib.ko",
    "fs/ext4/ext4-inode-test.ko",
    "fs/fat/fat_test.ko",
    "kernel/time/time_test.ko",
    "lib/kunit/kunit-example-test.ko",
    "lib/kunit/kunit-test.ko",
    "lib/kunit/platform-test.ko",
    # "mm/kfence/kfence_test.ko",
    "net/core/dev_addr_lists_test.ko",
    "sound/soc/soc-topology-test.ko",
    "sound/soc/soc-utils-test.ko",
]

# Modules defined by tools/testing/kunit/configs/android/kunit_clk_defconfig
_KUNIT_CLK_MODULES_LIST = [
    "drivers/clk/clk-gate_test.ko",
    "drivers/clk/clk-test.ko",
    "drivers/clk/clk_kunit_helpers.ko",
]

def _get_kunit_modules_list_minus_select(arch, map_each):
    """ Provides the list of KUnit modules, minus those in select() branches.

    Args:
        arch: One of [arm, arm64, i386, x86_64].
        map_each: A function that takes the module name as parameter, and returns
            the mapped value. If the module should be filtered out, the function
            should return None.
    Returns:
        The list of KUnit modules for the given |arch|.
    """
    if not arch in ("arm64", "x86_64", "arm", "i386"):
        fail("{}: arch {} not supported. Use one of [arm, arm64, i386, x86_64]".format(
            str(native.package_relative_label(":x")).removesuffix(":x"),
            arch,
        ))

    kunit_modules_list = _KUNIT_FRAMEWORK_MODULES + _KUNIT_COMMON_MODULES_LIST
    if arch == "arm":
        kunit_modules_list += _KUNIT_CLK_MODULES_LIST
    elif arch == "arm64":
        kunit_modules_list += _KUNIT_CLK_MODULES_LIST
    elif arch == "i386":
        kunit_modules_list.append("drivers/clk/clk_kunit_helpers.ko")
    elif arch == "x86_64":
        kunit_modules_list.append("drivers/clk/clk_kunit_helpers.ko")

    return _apply(map_each, kunit_modules_list)

# buildifier: disable=unnamed-macro
def get_kunit_modules_list(arch = None, map_each = None):
    """ Provides the list of KUnit modules.

    Args:
        arch: One of [arm, arm64, i386, x86_64].
        map_each: A function that takes the module name as parameter, and returns
            the mapped value. If the module should be filtered out, the function
            should return None.

    Returns:
        An opaque expression that represents the list of Kunit modules for the
        given |arch|. Do not treat the returned value as a list (e.g. use
        list comprehension); instead, use the |map_each| argument.
    """

    return select({
        "//conditions:default": _get_kunit_modules_list_minus_select(arch, map_each),
    })

# buildifier: disable=unnamed-macro
def get_kunit_modules_superset(arch = None, map_each = None):
    """Provides the list of superset of KUnit modules.

    This includes all modules on each branch of the conditionals.

    Args:
        arch: One of [arm, arm64, i386, x86_64].
        map_each: A function that takes the module name as parameter, and
            returns the mapped value. If the module should be filtered out, the
            function should return None.

    Returns:
        A list of superset of KUnit modules for the given |arch|.
    """
    return _get_kunit_modules_list_minus_select(arch, map_each)

_COMMON_UNPROTECTED_MODULES_LIST = []

# buildifier: disable=unused-variable
def get_gki_unprotected_modules_list(arch = None):
    return select({
        "//conditions:default": _COMMON_UNPROTECTED_MODULES_LIST,
    })

# buildifier: disable=unnamed-macro
def get_gki_kunit_modules(arch, page_size = None):
    """Returns the list of labels pointing to the GKI modules for KUnit.

    Args:
        arch: one of arm64, x86_64
        page_size: if arch is arm64, the page_size ("4k" or "16k")

    Returns:
        The list of labels pointing to the GKI modules for KUnit.
    """
    if arch == "arm64":
        if page_size == "16k":
            return get_kunit_modules_list(arch, map_each = lambda e: ":kernel_aarch64_16k/" + e)
        if page_size == "4k":
            return get_kunit_modules_list(arch, map_each = lambda e: ":kernel_aarch64/" + e)
    if arch == "x86_64":
        return get_kunit_modules_list(arch, map_each = lambda e: ":kernel_x86_64/" + e)

    fail("{}: arch {} (page_size {}) not supported. Use one of [arm64, x86_64]".format(
        str(native.package_relative_label(":x")).removesuffix(":x"),
        arch,
        page_size,
||||||| BASE   (2fe2a181bb39f6e443fc79f617ee4b0df9fe7d34 ANDROID: power_supply: Use fwnode to retrieve psy array from)
_ARM_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/ptp/ptp_kvm.ko",
]

_ARM64_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/char/hw_random/cctrng.ko",
    "drivers/misc/open-dice.ko",
    "drivers/ptp/ptp_kvm.ko",
]

_X86_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/ptp/ptp_kvm.ko",
]

_X86_64_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/ptp/ptp_kvm.ko",
]

def _apply(map_each, lst):
    if not map_each:
        return lst
    ret = []
    for elem in lst:
        mapped = map_each(elem)
        if mapped:
            ret.append(mapped)
    return ret

def _get_gki_modules_list_minus_select(arch, map_each):
    """ Provides the list of GKI modules, minus those in select() branches.

    Args:
        arch: One of [arm, arm64, i386, x86_64].
        map_each: A function that takes the module name as parameter, and returns
            the mapped value. If the module should be filtered out, the function
            should return None.

    Returns:
        The list of GKI modules for the given |arch|.
    """
    if not arch in ("arm64", "x86_64", "arm", "i386"):
        fail("{}: arch {} not supported. Use one of [arm, arm64, i386, x86_64]".format(
            str(native.package_relative_label(":x")).removesuffix(":x"),
            arch,
        ))

    if arch == "arm":
        return _apply(map_each, _COMMON_GKI_MODULES_LIST + _ARM_GKI_MODULES_LIST)

    if arch == "i386":
        return _apply(map_each, _COMMON_GKI_MODULES_LIST + _X86_GKI_MODULES_LIST)

    gki_modules_list = _apply(map_each, [] + _COMMON_GKI_MODULES_LIST)
    if arch == "arm64":
        gki_modules_list += _apply(map_each, _ARM64_GKI_MODULES_LIST)
    elif arch == "x86_64":
        gki_modules_list += _apply(map_each, _X86_64_GKI_MODULES_LIST)

    return gki_modules_list

# buildifier: disable=unnamed-macro
def get_gki_modules_list(arch = None, map_each = None):
    """Provides the list of GKI modules.

    Args:
        arch: One of [arm, arm64, i386, x86_64].
        map_each: A function that takes the module name as parameter, and
            returns the mapped value. If the module should be filtered out, the
            function should return None.

    Returns:
        An opaque expression that represents the list of GKI modules for the
        given |arch|. Do not treat the returned value as a list (e.g. use
        list comprehension); instead, use the |map_each| argument.
    """

    return select({
        "//conditions:default": _get_gki_modules_list_minus_select(arch, map_each),
    })

# buildifier: disable=unnamed-macro
def get_gki_modules_superset(arch = None, map_each = None):
    """Provides the list of superset of GKI modules.

    This includes all modules on each branch of the conditionals. For example,
    Rust modules may always be included regardless of the value of
    --kasan_sw_tags.

    Args:
        arch: One of [arm, arm64, i386, x86_64].
        map_each: A function that takes the module name as parameter, and
            returns the mapped value. If the module should be filtered out, the
            function should return None.

    Returns:
        A list that contains the superset of GKI modules for the given |arch|.
    """
    return _get_gki_modules_list_minus_select(arch, map_each)

_KUNIT_FRAMEWORK_MODULES = [
    "lib/kunit/kunit.ko",
]

# Modules defined by tools/testing/kunit/configs/android/kunit_defconfig
_KUNIT_COMMON_MODULES_LIST = [
    # keep sorted
    "drivers/android/tests/binder_alloc_kunit.ko",
    "drivers/base/regmap/regmap-kunit.ko",
    "drivers/base/regmap/regmap-ram.ko",
    "drivers/base/regmap/regmap-raw-ram.ko",
    "drivers/hid/hid-uclogic-test.ko",
    "drivers/iio/test/iio-test-format.ko",
    "drivers/input/tests/input_test.ko",
    "drivers/of/of_kunit_helpers.ko",
    "drivers/rtc/test_rtc_lib.ko",
    "fs/ext4/ext4-inode-test.ko",
    "fs/fat/fat_test.ko",
    "kernel/time/time_test.ko",
    "lib/kunit/kunit-example-test.ko",
    "lib/kunit/kunit-test.ko",
    "lib/kunit/platform-test.ko",
    # "mm/kfence/kfence_test.ko",
    "net/core/dev_addr_lists_test.ko",
    "sound/soc/soc-topology-test.ko",
    "sound/soc/soc-utils-test.ko",
]

# Modules defined by tools/testing/kunit/configs/android/kunit_clk_defconfig
_KUNIT_CLK_MODULES_LIST = [
    "drivers/clk/clk-gate_test.ko",
    "drivers/clk/clk-test.ko",
    "drivers/clk/clk_kunit_helpers.ko",
]

def _get_kunit_modules_list_minus_select(arch, map_each):
    """ Provides the list of KUnit modules, minus those in select() branches.

    Args:
        arch: One of [arm, arm64, i386, x86_64].
        map_each: A function that takes the module name as parameter, and returns
            the mapped value. If the module should be filtered out, the function
            should return None.
    Returns:
        The list of KUnit modules for the given |arch|.
    """
    if not arch in ("arm64", "x86_64", "arm", "i386"):
        fail("{}: arch {} not supported. Use one of [arm, arm64, i386, x86_64]".format(
            str(native.package_relative_label(":x")).removesuffix(":x"),
            arch,
        ))

    kunit_modules_list = _KUNIT_FRAMEWORK_MODULES + _KUNIT_COMMON_MODULES_LIST
    if arch == "arm":
        kunit_modules_list += _KUNIT_CLK_MODULES_LIST
    elif arch == "arm64":
        kunit_modules_list += _KUNIT_CLK_MODULES_LIST
    elif arch == "i386":
        kunit_modules_list.append("drivers/clk/clk_kunit_helpers.ko")
    elif arch == "x86_64":
        kunit_modules_list.append("drivers/clk/clk_kunit_helpers.ko")

    return _apply(map_each, kunit_modules_list)

# buildifier: disable=unnamed-macro
def get_kunit_modules_list(arch = None, map_each = None):
    """ Provides the list of KUnit modules.

    Args:
        arch: One of [arm, arm64, i386, x86_64].
        map_each: A function that takes the module name as parameter, and returns
            the mapped value. If the module should be filtered out, the function
            should return None.

    Returns:
        An opaque expression that represents the list of Kunit modules for the
        given |arch|. Do not treat the returned value as a list (e.g. use
        list comprehension); instead, use the |map_each| argument.
    """

    return select({
        "//conditions:default": _get_kunit_modules_list_minus_select(arch, map_each),
    })

# buildifier: disable=unnamed-macro
def get_kunit_modules_superset(arch = None, map_each = None):
    """Provides the list of superset of KUnit modules.

    This includes all modules on each branch of the conditionals.

    Args:
        arch: One of [arm, arm64, i386, x86_64].
        map_each: A function that takes the module name as parameter, and
            returns the mapped value. If the module should be filtered out, the
            function should return None.

    Returns:
        A list of superset of KUnit modules for the given |arch|.
    """
    return _get_kunit_modules_list_minus_select(arch, map_each)

_COMMON_UNPROTECTED_MODULES_LIST = []

# buildifier: disable=unused-variable
def get_gki_unprotected_modules_list(arch = None):
    return select({
        "//conditions:default": _COMMON_UNPROTECTED_MODULES_LIST,
    })

# buildifier: disable=unnamed-macro
def get_gki_kunit_modules(arch, page_size = None):
    """Returns the list of labels pointing to the GKI modules for KUnit.

    Args:
        arch: one of arm64, x86_64
        page_size: if arch is arm64, the page_size ("4k" or "16k")

    Returns:
        The list of labels pointing to the GKI modules for KUnit.
    """
    if arch == "arm64":
        if page_size == "16k":
            return get_kunit_modules_list(arch, map_each = lambda e: ":kernel_aarch64_16k/" + e)
        if page_size == "4k":
            return get_kunit_modules_list(arch, map_each = lambda e: ":kernel_aarch64/" + e)
    if arch == "x86_64":
        return get_kunit_modules_list(arch, map_each = lambda e: ":kernel_x86_64/" + e)

    fail("{}: arch {} (page_size {}) not supported. Use one of [arm64, x86_64]".format(
        str(native.package_relative_label(":x")).removesuffix(":x"),
        arch,
        page_size,
=======
def get_gki_modules_list(*args, **kwargs):
    # buildifier: disable=print
    print("""
WARNING: {this_modules_bzl} is deprecated. Load get_gki_modules_list() from {new_modules_bzl} instead.""".format(
        this_modules_bzl = str(Label(":modules.bzl")),
        new_modules_bzl = str(Label(":bazel/modules.bzl")),
>>>>>>> BRANCH (fd19bf440c846c791689a56ca9e2c4886a75c94a ANDROID: kleaf: move Bazel files to bazel/ folder.)
    ))
    return _get_gki_modules_list(*args, **kwargs)

def get_kunit_modules_list(*args, **kwargs):
    # buildifier: disable=print
    print("""
WARNING: {this_modules_bzl} is deprecated. Load get_kunit_modules_list() from {new_modules_bzl} instead.""".format(
        this_modules_bzl = str(Label(":modules.bzl")),
        new_modules_bzl = str(Label(":bazel/modules.bzl")),
    ))
    return _get_kunit_modules_list(*args, **kwargs)
