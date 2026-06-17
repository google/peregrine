load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

proto_library(
    name = "message_proto",
    srcs = ["message.proto"],
)

cc_proto_library(
    name = "message_cc_proto",
    deps = [":message_proto"],
)

cc_test(
    name = "message_test",
    srcs = ["message_test.cc"],
    deps = [
        ":message_cc_proto",
        "//src/api:types",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "parser",
    srcs = ["parser.cc"],
    hdrs = ["parser.h"],
    deps = [
        ":message_cc_proto",
        "//src/api:types",
        "//src/internal:assumptions",
    ],
)

cc_test(
    name = "parser_test",
    srcs = ["parser_test.cc"],
    deps = [
        ":parser",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)
