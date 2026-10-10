load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "rdma_util",
    srcs = ["rdma_util.cc"],
    hdrs = ["rdma_util.h"],
    deps = [
        "//:libibverbs",  # buildcleaner: keep
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/util:ipaddr",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "rdma_util_test",
    srcs = ["rdma_util_test.cc"],
    deps = [
        ":rdma_util",
        "//peregrine/src/util:ipaddr",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "rdma_context",
    srcs = ["rdma_context.cc"],
    hdrs = ["rdma_context.h"],
    deps = [
        "//:libibverbs",  # buildcleaner: keep
        "//peregrine/src/util:errno",
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "rdma_context_test",
    srcs = ["rdma_context_test.cc"],
    deps = [
        ":rdma_context",
        "//:libibverbs",  # buildcleaner: keep
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "rdma_qpair",
    srcs = ["rdma_qpair.cc"],
    hdrs = ["rdma_qpair.h"],
    deps = [
        ":rdma_context",
        "//:libibverbs",  # buildcleaner: keep
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "rdma_qpair_test",
    srcs = ["rdma_qpair_test.cc"],
    deps = [
        ":rdma_context",
        ":rdma_device",
        ":rdma_qpair",
        "//:libibverbs",  # buildcleaner: keep
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "rdma_device",
    srcs = ["rdma_device.cc"],
    hdrs = ["rdma_device.h"],
    deps = [
        ":rdma_context",
        "//:libibverbs",  # buildcleaner: keep
        "//peregrine/src/util:macro",
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
    name = "rdma_device_test",
    srcs = ["rdma_device_test.cc"],
    deps = [
        ":rdma_context",
        ":rdma_device",
        "//:libibverbs",  # buildcleaner: keep
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "rdma_memory",
    srcs = ["rdma_memory.cc"],
    hdrs = ["rdma_memory.h"],
    deps = [
        ":rdma_context",
        ":rdma_device",
        "//:libibverbs",  # buildcleaner: keep
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/util:errno",
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "rdma_memory_test",
    srcs = ["rdma_memory_test.cc"],
    deps = [
        ":rdma_device",
        ":rdma_memory",
        "//:libibverbs",  # buildcleaner: keep
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "rdma_manager",
    srcs = ["rdma_manager.cc"],
    hdrs = ["rdma_manager.h"],
    deps = [
        ":rdma_context",
        ":rdma_device",
        ":rdma_memory",
        ":rdma_qpair",
        "//:libibverbs",  # buildcleaner: keep
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:config",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/base:nicinfo",
        "//peregrine/src/internal/channel",
        "//peregrine/src/internal/channel:channel_util",
        "//peregrine/src/internal/control",
        "//peregrine/src/internal/control:message_cc_proto",
        "//peregrine/src/internal/control:message_internal_cc_proto",
        "//peregrine/src/util",
        "//peregrine/src/util:ipaddr",
        "//peregrine/src/util:nic",
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
    name = "rdma_manager_test",
    srcs = ["rdma_manager_test.cc"],
    deps = [
        ":rdma_device",
        ":rdma_manager",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:config",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/control",
        "@abseil-cpp//absl/status",
        "@googletest//:gtest_main",
    ],
)
