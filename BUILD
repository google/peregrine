load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

exports_files([
    "LICENSE",
    "README.md",
])

# System libibverbs dependency for RDMA transport.
cc_library(
    name = "libibverbs",
    linkopts = ["-libverbs"],
    visibility = ["//visibility:public"],
)
