load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "channel_types",
    hdrs = ["channel_types.h"],
)

cc_library(
    name = "channel",
    hdrs = ["channel.h"],
    deps = [
        ":channel_types",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:types",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "channel_test_util",
    testonly = True,
    srcs = ["channel_test_util.cc"],
    hdrs = ["channel_test_util.h"],
    deps = [
        ":channel",
        ":channel_msg",
        ":channel_stream",
        ":channel_util",
        ":pipe",
        "//peregrine/src/internal/socket:socket_test_util",
        "//peregrine/src/internal/util:test_param",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "channel_tcp",
    srcs = ["channel_tcp.cc"],
    hdrs = ["channel_tcp.h"],
    deps = [
        ":channel",
        ":channel_types",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/socket:socket_tcp",
        "//peregrine/src/internal/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "channel_udp",
    srcs = ["channel_udp.cc"],
    hdrs = ["channel_udp.h"],
    deps = [
        ":channel",
        ":channel_types",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/socket:socket_udp",
        "//peregrine/src/internal/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "pipe",
    testonly = True,
    srcs = ["pipe.cc"],
    hdrs = ["pipe.h"],
    deps = [
        "//peregrine/src/internal/util:test_iov",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "channel_msg",
    testonly = True,
    srcs = ["channel_msg.cc"],
    hdrs = ["channel_msg.h"],
    deps = [
        ":channel",
        ":channel_types",
        ":pipe",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/util",
        "//peregrine/src/internal/util:test_iov",
        "//peregrine/src/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "channel_stream",
    testonly = True,
    srcs = ["channel_stream.cc"],
    hdrs = ["channel_stream.h"],
    deps = [
        ":channel",
        ":channel_types",
        ":pipe",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/util",
        "//peregrine/src/internal/util:test_iov",
        "//peregrine/src/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "channel_test",
    srcs = ["channel_test.cc"],
    shard_count = 6,
    deps = [
        ":channel",
        ":channel_test_util",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/util:test_param",
        "//peregrine/src/util",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "channel_util",
    srcs = ["channel_util.cc"],
    hdrs = ["channel_util.h"],
    deps = [
        ":channel",
        ":channel_rdma",
        ":channel_tcp",
        ":channel_udp",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/rdma:rdma_queue_pair",
        "//peregrine/src/internal/socket:connector",
        "//peregrine/src/internal/socket:socket_tcp",
        "//peregrine/src/internal/socket:socket_udp",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "channel_util_test",
    srcs = ["channel_util_test.cc"],
    shard_count = 2,
    deps = [
        ":channel",
        ":channel_types",
        ":channel_util",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/base:nicinfo",
        "//peregrine/src/internal/rdma:rdma_device_context",
        "//peregrine/src/internal/rdma:rdma_device_manager",
        "//peregrine/src/internal/rdma:rdma_queue_pair",
        "//peregrine/src/internal/socket:acceptor",
        "//peregrine/src/internal/socket:socket_tcp",
        "//peregrine/src/internal/util:test_param",
        "//peregrine/src/internal/util:test_util",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "channel_rdma",
    srcs = ["channel_rdma.cc"],
    hdrs = ["channel_rdma.h"],
    deps = [
        ":channel",
        ":channel_types",
        "//:libibverbs",  # buildcleaner: keep
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/chunk",
        "//peregrine/src/internal/chunk:chunk_flatbuf",
        "//peregrine/src/internal/rdma:rdma_queue_pair",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "channel_rdma_test",
    srcs = ["channel_rdma_test.cc"],
    tags = [
        "local",
        "manual",
    ],
    deps = [
        ":channel_rdma",
        ":channel_types",
        "//:libibverbs",  # buildcleaner: keep
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/chunk",
        "//peregrine/src/internal/chunk:chunk_flatbuf",
        "//peregrine/src/internal/rdma:rdma_device_context",
        "//peregrine/src/internal/rdma:rdma_device_manager",
        "//peregrine/src/internal/rdma:rdma_memory_manager",
        "//peregrine/src/internal/rdma:rdma_queue_pair",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)
