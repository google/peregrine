# For integration workloads only.

load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(
    default_testonly = True,
    default_visibility = ["//test/integration:__subpackages__"],
)

cc_library(
    name = "batch-generator",
    hdrs = ["batch-generator.h"],
    deps = [
        "//src/api:transport_types",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "serial-fixed-write",
    srcs = ["serial-fixed-write.cc"],
    hdrs = ["serial-fixed-write.h"],
    deps = [
        ":batch-generator",
        "//src/api:transport_types",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
    ],
)
