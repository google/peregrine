load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

exports_files([
    "LICENSE",
    "README.md",
])

cc_library(
    name = "libibverbs",  # RDMA
    linkopts = ["-libverbs"],
    visibility = ["//visibility:public"],
)
