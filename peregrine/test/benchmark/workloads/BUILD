# For benchmark workloads only.

load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(
    default_testonly = True,
    default_visibility = ["//peregrine/test/benchmark:__subpackages__"],
)

cc_library(
    name = "workload_generator",
    hdrs = ["workload_generator.h"],
    deps = [
        "//peregrine/src/api:transport_types",
    ],
)

cc_library(
    name = "serial_fixed_write",
    srcs = ["serial_fixed_write.cc"],
    hdrs = ["serial_fixed_write.h"],
    deps = [
        ":workload_generator",
        "//peregrine/src/api:transport_types",
        "//peregrine/test/benchmark:flags",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "kv_cache",
    srcs = ["kv_cache.cc"],
    hdrs = ["kv_cache.h"],
    deps = [
        ":workload_generator",
        "//peregrine/src/api:transport_types",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "workload_util",
    hdrs = ["workload_util.h"],
    deps = [
        ":kv_cache",
        ":serial_fixed_write",
        ":workload_generator",
        "//peregrine/test/benchmark:types",
        "@abseil-cpp//absl/log",
    ],
)
