load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

exports_files([
    "LICENSE",
    "README.md",
])

# RDMA is opt-in (--define=peregrine_rdma=true); default is TCP-only, no libibverbs.
config_setting(
    name = "rdma_enabled",
    values = {"define": "peregrine_rdma=true"},
)

# libibverbs: headers always; -libverbs only with :rdma_enabled, otherwise
# PEREGRINE_NO_RDMA makes rdma/ibv_loader.h compile stubs instead of ibv_* calls.
cc_library(
    name = "libibverbs",  # RDMA
    defines = select({
        ":rdma_enabled": [],
        "//conditions:default": ["PEREGRINE_NO_RDMA"],
    }),
    linkopts = select({
        ":rdma_enabled": ["-libverbs"],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)
