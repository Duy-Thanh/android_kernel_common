# SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
# Copyright (C) 2023 The Android Open Source Project

"""
ABI aware build rules.
"""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@bazel_skylib//rules:native_binary.bzl", "native_binary")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load(
    "@rules_pkg//pkg:mappings.bzl",
    "pkg_files",
    "strip_prefix",
)
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("//build/kernel/kleaf:hermetic_tools.bzl", "hermetic_genrule")
load(
    "//build/kernel/kleaf:kernel.bzl",
    "android_filegroup",
)

visibility("private")

_ALL_ABIS = ["arm", "arm64", "x86", "x86_64"]

def _copy_with_abi(
        name,
        visibility = None,
        path_prefix = None,
        abis = None,
        out = None):
    if not path_prefix:
        path_prefix = ""
    if not abis:
        abis = _ALL_ABIS
    if not out:
        out = name

    for abi in abis:
        cpu = abi
        if abi == "x86":
            cpu = "i386"
        android_filegroup(
            name = "{name}_{abi}_bin".format(name = name, abi = abi),
            srcs = [":{name}".format(name = name)],
            cpu = cpu,
            visibility = visibility,
        )
        pkg_files(
            name = "{name}_{abi}".format(name = name, abi = abi),
            srcs = [":{name}_{abi}_bin".format(name = name, abi = abi)],
            renames = {
                ":{name}_{abi}_bin".format(name = name, abi = abi): paths.join(path_prefix, abi, out),
            },
            strip_prefix = strip_prefix.from_pkg(),
            visibility = visibility,
        )

def cc_binary_with_abi(
        name,
        path_prefix = None,
        abis = None,
        visibility = None,
        out = None,
        **kwargs):
    """A cc_binary replacement that generates output in each subdirectory named by abi.

    For example:
    ```
    cc_binary_with_abi(
        name = "a_binary",
        abis = ["x86_64", "arm64"],
        path_prefix = "my/path",
    )
    ```
    generates 2 rules:
    * Rule a_binary_x86_64: Builds the cc_binary and put output in my/path/x86_64/a_binary.
    * Rule a_binary_arm64: Builds the cc_binary and put output in my/path/arm64/a_binary.

    Args:
        name: the name of the build rule.
        path_prefix: [Nonconfigurable](https://bazel.build/reference/be/common-definitions#configurable-attributes).
            The path prefix to attach to output.
        abis: [Nonconfigurable](https://bazel.build/reference/be/common-definitions#configurable-attributes).
            The intended abis to generate. Default is arm64 & x86_64.
        visibility: [Nonconfigurable](https://bazel.build/reference/be/common-definitions#configurable-attributes).
            The visibility attribute on a target controls whether the target can be used in other packages.
        out: [Nonconfigurable](https://bazel.build/reference/be/common-definitions#configurable-attributes).
            The output filename. Default is `name`.
        **kwargs: the rest args that cc_binary uses.
    """
    cc_binary(
        name = name,
        visibility = visibility,
        **kwargs
    )

    _copy_with_abi(
        name = name,
        path_prefix = path_prefix,
        abis = abis,
        visibility = visibility,
        out = out,
    )

def sh_binary_with_abi(
        name,
        path_prefix = None,
        abis = None,
        visibility = None,
        out = None,
        **kwargs):
    """A sh_binary replacement that generates output in each subdirectory named by abi.

    For example:
    ```
      sh_binary_with_abi(
        name = "a_binary",
        abis = ["x86_64", "arm64"],
        path_prefix = "my/path",
      )
    ```
    generates 2 rules:
    * Rule a_binary_x86_64: Copies a_binary and put output in my/path/x86_64/a_binary.
    * Rule a_binary_arm64: Copies a_binary and put output in my/path/arm64/a_binary.

    Args:
        name: the name of the build rule.
        path_prefix: [Nonconfigurable](https://bazel.build/reference/be/common-definitions#configurable-attributes).
          The path prefix to attach to output.
        abis: [Nonconfigurable](https://bazel.build/reference/be/common-definitions#configurable-attributes).
          The intended abis to generate. Default is arm64 & x86_64.
        visibility: [Nonconfigurable](https://bazel.build/reference/be/common-definitions#configurable-attributes).
          The visibility attribute on a target controls whether the target can be used in other packages.
        out: [Nonconfigurable](https://bazel.build/reference/be/common-definitions#configurable-attributes).
          The output filename. Default is `name`.
        **kwargs: the rest args that native_binary uses.
    """
    if not out:
        out = name

    # Uses native_binary instead of sh_binary because sh_binary is not
    # compatible with copy_file (sh_binary generates more than 1 outs).
    native_binary(
        name = name,
        visibility = visibility,
        out = out,
        **kwargs
    )

    _copy_with_abi(
        name = name,
        path_prefix = path_prefix,
        abis = abis,
        visibility = visibility,
        out = out,
    )

def multi_kmi_update(name, targets):
    """Creates a single target to update multiple ABI definitions.

    Args:
        name: The name of the resulting sh_binary target.
        targets: A list of abi_update targets (e.g. [":kernel_aarch64_abi_update"])
    """

    script_name = name + "_script"

    commands = ["echo '#!/bin/bash' > $@"]
    for t in targets:
        commands.append("echo '$(rootpath {t}) \"$$@\" ;' >> $@".format(t = t))

    hermetic_genrule(
        name = script_name,
        srcs = targets,
        outs = [name + ".sh"],
        cmd = " && ".join(commands),
    )
    sh_binary(
        name = name,
        srcs = [":" + script_name],
        data = targets,
    )
