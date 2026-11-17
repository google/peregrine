load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "tcp_manager",
    hdrs = ["tcp_manager.h"],
    deps = [
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/channel",
        "//peregrine/src/internal/event:poller",
        "//peregrine/src/internal/nic:nic_inventory",
        "//peregrine/src/internal/socket:socket_tcp",
        "//peregrine/src/util:macro",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "channel_manager",
    hdrs = ["channel_manager.h"],
    deps = [
        ":tcp_manager",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:config",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/channel",
        "//peregrine/src/internal/metrics:engine_metrics",
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
    ],
)
