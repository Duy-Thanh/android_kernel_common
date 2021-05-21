filegroup(
    name = "sources",
    srcs = glob(["**"]),
    visibility = ["//visibility:public"],
)

genrule(
    name = "kernel_aarch64",
    srcs = [
        "//build:kernel-build-scripts",
        "//common:sources",
        "//prebuilts-master/clang/host/linux-x86/clang-r416183b:all",
        "//prebuilts/build-tools:linux-x86",
        "//prebuilts/kernel-build-tools:linux-x86",
    ],
    outs = [
        "abi.prop",
        "Image",
        "Image.lz4",
        "System.map",
        "modules.builtin",
        "modules.builtin.modinfo",
        "kernel-headers.tar.gz",
        "kernel-uapi-headers.tar.gz",
        "vmlinux",
        "vmlinux.btf",
        "vmlinux.symvers",
    ],
    cmd = "DIST_DIR=$$(dirname $(location vmlinux)) KERNEL_DIR=common BUILD_CONFIG=common/build.config.gki.aarch64 build/build.sh",
)
