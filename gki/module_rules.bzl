# Copyright (C) 2025 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Rules for declaring GKI module targets."""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("//build/kernel/kleaf:kernel.bzl", "kernel_build_output")

visibility("private")

def _gki_out(kernel_build, **kwargs):
    kernel_build = native.package_relative_label(kernel_build)

    def fn(module_name):
        kernel_build_output(
            name = kernel_build.name + "/" + module_name,
            out = module_name,
            kernel_build = kernel_build,
            **kwargs
        )

        if paths.basename(module_name) != module_name:
            native.alias(
                name = kernel_build.name + "/" + paths.basename(module_name),
                actual = kernel_build.name + "/" + module_name,
                **kwargs
            )

    return fn

def _add_prefix(prefix):
    return lambda s: prefix + s

def gki_all_modules(name, kernel_build, arch, list_functions, **kwargs):
    """Declare targets that refers to GKI modules.

    This declares targets like

    *   kernel_aarch64_modules
    *   kernel_aarch64/zram.ko
    *   kernel_aarch64/drivers/block/zram/zram.ko

    Args:
        name: name of the top-level target, e.g. kernel_aarch64_modules
        kernel_build: the common_kernel()
        arch: arch of the common_kernel()
        list_functions: A list of functions that return a list of module names;
            see modules.bzl
        **kwargs: extra kwargs to declared targets, e.g. visibility.
    """
    kernel_build = native.package_relative_label(kernel_build)
    srcs = []
    for list_fn in list_functions:
        list_fn(arch, map_each = _gki_out(
            kernel_build = kernel_build,
            **kwargs
        ))
        srcs += list_fn(arch, map_each = _add_prefix(kernel_build.name + "/"))

    native.filegroup(
        name = name,
        srcs = srcs,
        **kwargs
    )

def _gki_protected_module_names_impl(ctx):
    module_names = sorted([
        module_name.removesuffix(".ko")
        for module_name in ctx.attr.module_names
        if module_name not in ctx.attr.exclude
    ])
    out = ctx.actions.declare_file(ctx.attr.out)
    ctx.actions.write(out, "\n".join(module_names) + "\n")
    return DefaultInfo(files = depset([out]))

gki_protected_module_names = rule(
    implementation = _gki_protected_module_names_impl,
    attrs = {
        "out": attr.string(mandatory = True),
        "module_names": attr.string_list(),
        "exclude": attr.string_list(),
    },
)
