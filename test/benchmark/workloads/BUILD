# For benchmark workloads only.

load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(
    default_testonly = True,
    default_visibility = ["//test/benchmark:__subpackages__"],
)

cc_library(
    name = "workload_generator",
    hdrs = ["workload_generator.h"],
    deps = ["@abseil-cpp//absl/status"],
)

cc_library(
    name = "serial_fixed_write",
    srcs = ["serial_fixed_write.cc"],
    hdrs = ["serial_fixed_write.h"],
    deps = [
        ":workload_generator",
        "//src/api:transport",
        "//src/api:transport_types",
        "//src/util",
        "//test/benchmark:control_cc_proto",
        "//test/benchmark:control_util",
        "//test/benchmark:flags",
        "//test/benchmark:types",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "workload_util",
    hdrs = ["workload_util.h"],
    deps = [
        ":serial_fixed_write",
        ":workload_generator",
        "//src/api:transport",
        "//test/benchmark:types",
        "@abseil-cpp//absl/log",
    ],
)
