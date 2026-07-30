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
        ":message",
        ":message_cc_proto",
        "//src/api:transport_types",
        "//src/internal:assumptions",
        "//src/internal/base:types",
        "//src/internal/channel",
        "//src/internal/channel:channel_types",
        "//src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "control_test",
    srcs = ["control_test.cc"],
    deps = [
        ":control",
        ":message",
        "//src/internal/channel:channel_test_util",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "rpc_client",
    srcs = ["rpc_client.cc"],
    hdrs = ["rpc_client.h"],
    deps = [
        ":message_cc_proto",
        ":service_cc_grpc_proto",
        "//src/util:macro",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@grpc//:grpc++",
    ],
)

cc_library(
    name = "rpc_server",
    srcs = ["rpc_server.cc"],
    hdrs = ["rpc_server.h"],
    deps = [
        ":message_cc_proto",
        ":service_cc_grpc_proto",
        "//src/util:macro",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@grpc//:grpc++",
    ],
)

cc_test(
    name = "rpc_test",
    srcs = ["rpc_test.cc"],
    deps = [
        ":message_cc_proto",
        ":rpc_client",
        ":rpc_server",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@googletest//:gtest_main",
        "@grpc//:grpc++",
    ],
)
