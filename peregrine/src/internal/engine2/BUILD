load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "assumptions",
    hdrs = ["assumptions.h"],
)

cc_library(
    name = "peer_discovery",
    hdrs = ["peer_discovery.h"],
    deps = [
        ":assumptions",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "engine",
    srcs = ["engine.cc"],
    hdrs = ["engine.h"],
    deps = [
        ":assumptions",
        ":channel_manager",
        ":peer_discovery",
        ":post",
        ":transfer_manager",
        "//peregrine/src/api:transport_metrics",
        "//peregrine/src/api:transport_trace",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/internal/base:config",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/metrics:engine_metrics",
        "//peregrine/src/internal/trace:chrome_trace_exporter",
        "//peregrine/src/internal/trace:trace_context",
        "//peregrine/src/internal/trace:trace_recorder",
        "//peregrine/src/util",
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "engine_test",
    srcs = ["engine_test.cc"],
    deps = [
        ":engine",
        ":peer_discovery",
        "//peregrine/src/api:transport_metrics",
        "//peregrine/src/api:transport_trace",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:config",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "transfer_manager",
    srcs = ["transfer_manager.cc"],
    hdrs = ["transfer_manager.h"],
    deps = [
        ":assumptions",
        ":channel_manager",
        ":peer_discovery",
        ":peer_state",
        ":post",
        ":worker",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:config",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/channel",
        "//peregrine/src/internal/nic:nic_inventory",
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "worker",
    srcs = ["worker.cc"],
    hdrs = ["worker.h"],
    deps = [
        ":peer_state",
        "//peregrine/src/internal/channel",
        "//peregrine/src/util:macro",
    ],
)

cc_test(
    name = "worker_test",
    srcs = ["worker_test.cc"],
    deps = [
        ":peer_state",
        ":worker",
        "//peregrine/src/internal/channel",
        "//peregrine/src/internal/channel:channel_type",
        "//peregrine/src/internal/lib:iovec_cursor",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "transfer_manager_test",
    srcs = ["transfer_manager_test.cc"],
    deps = [
        ":channel_manager",
        ":peer_discovery",
        ":post",
        ":transfer_manager",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:config",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "post",
    hdrs = ["post.h"],
    deps = [
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/trace:trace_context",
    ],
)

cc_library(
    name = "peer_state",
    srcs = ["peer_state.cc"],
    hdrs = ["peer_state.h"],
    deps = [
        ":assumptions",
        ":post",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/util:macro",
    ],
)

cc_test(
    name = "peer_state_test",
    srcs = ["peer_state_test.cc"],
    deps = [
        ":peer_state",
        ":post",
        "//peregrine/src/api:transport_types",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "channel_manager",
    srcs = ["channel_manager.cc"],
    hdrs = ["channel_manager.h"],
    deps = [
        ":assumptions",
        "//peregrine/src/internal/base:config",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/channel",
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "channel_manager_test",
    srcs = ["channel_manager_test.cc"],
    deps = [
        ":channel_manager",
        "//peregrine/src/internal/base:config",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/channel",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@googletest//:gtest_main",
    ],
)
