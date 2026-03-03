<<<<<<< HEAD   (d8f2eda3a65a34273481d9d2a5b573235ffe1485 ANDROID: bazel: disable rust on --kasan_sw_tags. am: e9fa963)
# SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
# Copyright (C) 2025 The Android Open Source Project
"""Re-exports of symbols for external usage regarding to lists of modules.
"""

load(
    ":bazel/modules_private.bzl",
    _get_gki_modules_list = "get_gki_modules_list",
    _get_kunit_modules_list = "get_kunit_modules_list",
)

visibility("public")
get_gki_modules_list = _get_gki_modules_list
get_kunit_modules_list = _get_kunit_modules_list
||||||| BASE   (e9fa963b5cc0f4877306ea503e92e1f11b71bb5a ANDROID: bazel: disable rust on --kasan_sw_tags.)
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

def get_gki_modules_list(*args, **kwargs):
    # buildifier: disable=print
    print("""
WARNING: {this_modules_bzl} is deprecated. Load get_gki_modules_list() from {new_modules_bzl} instead.""".format(
        this_modules_bzl = str(Label(":modules.bzl")),
        new_modules_bzl = str(Label(":bazel/modules.bzl")),
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
=======
>>>>>>> BRANCH (530e8fddbee20b90f3180ef48903a394d8633638 ANDROID: bazel: delete top-level modules.bzl.)
