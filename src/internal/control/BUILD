load("@grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
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

proto_library(
    name = "service_proto",
    srcs = ["service.proto"],
    deps = [":message_proto"],
)

cc_proto_library(
    name = "service_cc_proto",
    deps = [":service_proto"],
)

cc_grpc_library(
    name = "service_cc_grpc_proto",
    srcs = [":service_proto"],
    grpc_only = True,
    deps = [":service_cc_proto"],
)

cc_library(
    name = "message",
    srcs = ["message.cc"],
    hdrs = ["message.h"],
    deps = [
        ":message_cc_proto",
        "//src/api:transport_types",
        "//src/internal:assumptions",
        "//src/internal/base:endpoint",
        "//src/internal/base:hostinfo",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "message_test",
    srcs = ["message_test.cc"],
    deps = [
        ":message",
        ":message_cc_proto",
        "//src/api:transport_types",
        "//src/internal/base:endpoint",
        "//src/internal/base:hostinfo",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "control",
    srcs = ["control.cc"],
    hdrs = ["control.h"],
    deps = [
        ":grpc_client",
        ":grpc_server",
        ":message_cc_proto",
        "//src/internal/base:endpoint",
        "//src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
        "@grpc//:grpc++",
    ],
)

cc_test(
    name = "control_test",
    srcs = ["control_test.cc"],
    shard_count = 2,
    deps = [
        ":control",
        ":message",
        ":message_cc_proto",
        "//src/api:transport_types",
        "//src/internal/base:endpoint",
        "//src/internal/base:hostinfo",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@googletest//:gtest_main",
        "@grpc//:grpc++",
    ],
)

cc_library(
    name = "grpc_client",
    srcs = ["grpc_client.cc"],
    hdrs = ["grpc_client.h"],
    deps = [
        ":message_cc_proto",
        ":service_cc_grpc_proto",
        "//src/internal/base:endpoint",
        "//src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@grpc//:grpc++",
    ],
)

cc_library(
    name = "grpc_server",
    srcs = ["grpc_server.cc"],
    hdrs = ["grpc_server.h"],
    deps = [
        ":message_cc_proto",
        ":service_cc_grpc_proto",
        "//src/internal/base:endpoint",
        "//src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@grpc//:grpc++",
    ],
)

cc_test(
    name = "grpc_test",
    srcs = ["grpc_test.cc"],
    deps = [
        ":grpc_client",
        ":grpc_server",
        ":message_cc_proto",
        "//src/internal/base:endpoint",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
        "@grpc//:grpc++",
    ],
)
