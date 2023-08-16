load("@bazel_skylib//rules:copy_file.bzl", "copy_file")

DEFAULT_ABIS = ["x86_64", "arm64"]

def _build_with_abi(name, build_rule, abis = DEFAULT_ABIS, **kwargs):
    build_rule(name = name, **kwargs)

    for abi in abis:
        copy_file(
            name = "{name}_{abi}".format(name = name, abi = abi),
            src = ":{name}".format(name = name),
            out = "{abi}/{name}".format(name = name, abi = abi),
        )

def cc_binary_with_abi(name, abis = DEFAULT_ABIS, **kwargs):
    _build_with_abi(
        name = name,
        build_rule = native.cc_binary,
        abis = abis,
        **kwargs
    )
