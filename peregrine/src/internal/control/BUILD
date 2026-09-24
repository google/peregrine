load("@grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

proto_library(
    name = "message_internal_proto",
    srcs = ["message_internal.proto"],
)

cc_proto_library(
    name = "message_internal_cc_proto",
    deps = [":message_internal_proto"],
)

proto_library(
    name = "message_proto",
    srcs = ["message.proto"],
    deps = [":message_internal_proto"],
)

cc_proto_library(
    name = "message_cc_proto",
    deps = [":message_proto"],
)

proto_library(
    name = "service_proto",
    srcs = ["service.proto"],
    deps = [":message_proto"],  # Do NOT add any new dependencies here!
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
        ":message_internal_cc_proto",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/base:nicinfo",
        "//peregrine/src/internal/socket/psp",
        "//peregrine/src/util:nic",
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
        ":message_internal_cc_proto",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/base:nicinfo",
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
        ":message",
        ":message_cc_proto",
        ":message_internal_cc_proto",
        "//peregrine/src/api:transport_metrics",
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/internal:coding_style",
        "//peregrine/src/internal/base:config",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/metrics:control_metrics",
        "//peregrine/src/internal/socket/psp",
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
        "@grpc//:grpc++",
    ],
)

cc_test(
    name = "control_test",
    srcs = ["control_test.cc"],
    shard_count = 2,
    deps = [
        ":control",
        ":message_cc_proto",
        ":message_internal_cc_proto",
        "//peregrine/src/internal/base:config",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/base:nicinfo",
        "//peregrine/src/internal/socket/psp",
        "//peregrine/src/util",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
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
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
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
        ":service_cc_proto",
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/util:macro",
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
        "//peregrine/src/internal/base:endpoint",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
        "@grpc//:grpc++",
    ],
)
