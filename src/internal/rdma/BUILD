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
