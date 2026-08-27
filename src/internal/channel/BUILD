load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "channel_types",
    hdrs = ["channel_types.h"],
)

cc_library(
    name = "channel",
    hdrs = ["channel.h"],
    deps = [
        ":channel_types",
        "//src/api:transport_types",
        "//src/internal/base:types",
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
        "//src/internal/socket:socket_test_util",
        "//src/internal/util:test_param",
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
        "//src/api:transport_types",
        "//src/internal/base:types",
        "//src/internal/socket:socket_tcp",
        "//src/internal/util",
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
        "//src/api:transport_types",
        "//src/internal/base:types",
        "//src/internal/socket:socket_udp",
        "//src/internal/util",
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
        "//src/internal/util:test_iov",
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
        "//src/api:transport_types",
        "//src/internal/base:types",
        "//src/internal/util",
        "//src/internal/util:test_iov",
        "//src/util",
        "//util/random:shared_bit_gen",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
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
        "//src/api:transport_types",
        "//src/internal/base:types",
        "//src/internal/util",
        "//src/internal/util:test_iov",
        "//src/util",
        "//util/random:shared_bit_gen",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
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
        "//src/api:transport_types",
        "//src/internal/base:types",
        "//src/internal/util:test_param",
        "//src/util",
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
        ":channel_tcp",
        ":channel_udp",
        "//src/internal/base:endpoint",
        "//src/internal/socket:connector",
        "//src/internal/socket:socket_tcp",
        "//src/internal/socket:socket_udp",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "channel_util_test",
    srcs = ["channel_util_test.cc"],
    shard_count = 2,
    deps = [
        ":channel_util",
        "//src/internal/base:endpoint",
        "//src/internal/base:hostinfo",
        "//src/internal/socket:acceptor",
        "//src/internal/socket:socket_tcp",
        "//src/internal/util:test_util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
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
        "//src/api:transport_types",
        "//src/internal/base:types",
        "//src/internal/chunk",
        "//src/internal/chunk:chunk_flatbuf",
        "//src/internal/rdma:rdma_queue_pair",
        "//src/util:macro",
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
        "//src/api:transport_types",
        "//src/internal/base:types",
        "//src/internal/chunk",
        "//src/internal/chunk:chunk_flatbuf",
        "//src/internal/rdma:rdma_device_context",
        "//src/internal/rdma:rdma_device_manager",
        "//src/internal/rdma:rdma_memory_manager",
        "//src/internal/rdma:rdma_queue_pair",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)
