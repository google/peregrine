# For benchmarks only.

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_testonly = True,
    default_visibility = ["//visibility:private"],
)

cc_library(
    name = "flags",
    srcs = ["flags.cc"],
    hdrs = ["flags.h"],
    deps = [
        ":types",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "types",
    srcs = ["types.cc"],
    hdrs = ["types.h"],
    deps = [
        "//third_party/gloop/util/intops",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "types_test",
    srcs = ["types_test.cc"],
    deps = [
        ":types",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "peregrine_util",
    srcs = ["peregrine_util.cc"],
    hdrs = ["peregrine_util.h"],
    deps = [
        ":types",
        "//src/api:transport",
        "//src/api:transport_types",
        "//src/api:transport_util",
        "//src/util",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_binary(
    name = "peregrine",
    srcs = ["peregrine.cc"],
    deps = [
        ":flags",
        ":peregrine_util",
        ":types",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log:initialize",
    ],
)
