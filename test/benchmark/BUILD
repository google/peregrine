# For benchmarks only.

load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
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

proto_library(
    name = "control_proto",
    srcs = ["control.proto"],
)

cc_proto_library(
    name = "control_cc_proto",
    deps = [":control_proto"],
)

cc_library(
    name = "control_util",
    srcs = ["control_util.cc"],
    hdrs = ["control_util.h"],
    deps = [
        ":control_cc_proto",
        "//third_party/protobuf",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "peregrine_util",
    srcs = ["peregrine_util.cc"],
    hdrs = ["peregrine_util.h"],
    deps = [
        ":control_cc_proto",
        ":control_util",
        ":flags",
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
    visibility = ["//visibility:public"],
    deps = [
        ":flags",
        ":peregrine_util",
        ":types",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log:initialize",
    ],
)
