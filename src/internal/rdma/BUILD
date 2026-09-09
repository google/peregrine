load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "rdma_device_context",
    srcs = ["rdma_device_context.cc"],
    hdrs = ["rdma_device_context.h"],
    deps = [
        "//:libibverbs",  # buildcleaner: keep
        "//src/util:macro",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "rdma_device_context_test",
    srcs = ["rdma_device_context_test.cc"],
    deps = [
        ":rdma_device_context",
        "//:libibverbs",  # buildcleaner: keep
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "rdma_device_manager",
    srcs = ["rdma_device_manager.cc"],
    hdrs = ["rdma_device_manager.h"],
    deps = [
        ":rdma_device_context",
        "//:libibverbs",  # buildcleaner: keep
        "//src/util:macro",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "rdma_device_manager_test",
    srcs = ["rdma_device_manager_test.cc"],
    deps = [
        ":rdma_device_context",
        ":rdma_device_manager",
        "//:libibverbs",  # buildcleaner: keep
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "rdma_memory_manager",
    srcs = ["rdma_memory_manager.cc"],
    hdrs = ["rdma_memory_manager.h"],
    deps = [
        ":rdma_device_context",
        ":rdma_device_manager",
        "//:libibverbs",  # buildcleaner: keep
        "//src/internal:assumptions",
        "//src/util:macro",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "rdma_memory_manager_test",
    srcs = ["rdma_memory_manager_test.cc"],
    deps = [
        ":rdma_device_context",
        ":rdma_device_manager",
        ":rdma_memory_manager",
        "//:libibverbs",  # buildcleaner: keep
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "rdma_queue_pair",
    srcs = ["rdma_queue_pair.cc"],
    hdrs = ["rdma_queue_pair.h"],
    deps = [
        ":rdma_device_context",
        "//:libibverbs",  # buildcleaner: keep
        "//src/util:macro",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "rdma_queue_pair_test",
    srcs = ["rdma_queue_pair_test.cc"],
    deps = [
        ":rdma_device_context",
        ":rdma_device_manager",
        ":rdma_queue_pair",
        "//:libibverbs",  # buildcleaner: keep
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "rdma_acceptor",
    srcs = ["rdma_acceptor.cc"],
    hdrs = ["rdma_acceptor.h"],
    deps = [
        ":rdma_device_context",
        ":rdma_device_manager",
        ":rdma_memory_manager",
        ":rdma_queue_pair",
        "//:libibverbs",  # buildcleaner: keep
        "//src/api:transport_types",
        "//src/internal/base:config",
        "//src/internal/base:endpoint",
        "//src/internal/base:hostinfo",
        "//src/internal/channel",
        "//src/internal/channel:channel_util",
        "//src/internal/control",
        "//src/internal/control:message_cc_proto",
        "//src/internal/control:message_internal_cc_proto",
        "//src/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "rdma_acceptor_test",
    srcs = ["rdma_acceptor_test.cc"],
    deps = [
        ":rdma_acceptor",
        ":rdma_device_manager",
        "//src/api:transport_types",
        "//src/internal/base:config",
        "//src/internal/base:endpoint",
        "//src/internal/base:hostinfo",
        "//src/internal/control",
        "@abseil-cpp//absl/status",
        "@googletest//:gtest_main",
    ],
)
