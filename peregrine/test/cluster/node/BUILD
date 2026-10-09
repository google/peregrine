# Multi-instance C++ node agent and libraries for Peregrine cluster benchmarks.

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_testonly = True,
    default_visibility = ["//peregrine/test/cluster:__subpackages__"],
)

cc_library(
    name = "config",
    hdrs = ["config.h"],
    deps = [
        "//peregrine/src/api:transport_types",
        "//peregrine/test/workloads:workload_generator",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "affinity",
    srcs = ["affinity.cc"],
    hdrs = ["affinity.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "affinity_test",
    srcs = ["affinity_test.cc"],
    deps = [
        ":affinity",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "topology",
    srcs = ["topology.cc"],
    hdrs = ["topology.h"],
    deps = [
        ":config",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "topology_test",
    srcs = ["topology_test.cc"],
    deps = [
        ":config",
        ":topology",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "metrics",
    srcs = ["metrics.cc"],
    hdrs = ["metrics.h"],
    deps = [
        "//peregrine/src/api:transport_metrics",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "metrics_test",
    srcs = ["metrics_test.cc"],
    deps = [
        ":metrics",
        "//peregrine/src/api:transport_metrics",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "control_channel",
    srcs = ["control_channel.cc"],
    hdrs = ["control_channel.h"],
    deps = [
        ":config",
        ":metrics",
        ":topology",
        "//peregrine/src/api:transport_metrics",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "control_channel_test",
    srcs = ["control_channel_test.cc"],
    deps = [
        ":config",
        ":control_channel",
        ":metrics",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "instance",
    srcs = ["instance.cc"],
    hdrs = ["instance.h"],
    deps = [
        ":config",
        "//peregrine/src/api:transport",
        "//peregrine/src/api:transport_metrics",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/api:transport_util",
        "//peregrine/src/util",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "instance_test",
    srcs = ["instance_test.cc"],
    deps = [
        ":config",
        ":instance",
        "//peregrine/src/util",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "cluster_node",
    srcs = ["cluster_node.cc"],
    hdrs = ["cluster_node.h"],
    deps = [
        ":affinity",
        ":config",
        ":control_channel",
        ":instance",
        ":metrics",
        ":topology",
        "//peregrine/src/api:transport_metrics",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/util:thread",
        "//peregrine/test/workloads:workload_generator",
        "//peregrine/test/workloads:workload_util",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "cluster_node_test",
    srcs = ["cluster_node_test.cc"],
    deps = [
        ":cluster_node",
        ":config",
        ":control_channel",
        "//peregrine/src/util",
        "//peregrine/src/util:thread",
        "//peregrine/test/workloads:serial_fixed_write",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "flags",
    srcs = ["flags.cc"],
    hdrs = ["flags.h"],
    deps = [
        ":affinity",
        ":config",
        ":topology",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/util:ipaddr",
        "//peregrine/src/util:nic",
        "//peregrine/test/workloads:workload_generator",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "flags_test",
    srcs = ["flags_test.cc"],
    deps = [
        ":config",
        ":flags",
        "@abseil-cpp//absl/flags:flag",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "node",
    srcs = ["node_main.cc"],
    deps = [
        ":cluster_node",
        ":control_channel",
        ":flags",
        ":instance",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
    ],
)
