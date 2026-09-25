load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "socket_util",
    srcs = ["socket_util.cc"],
    hdrs = ["socket_util.h"],
    deps = [
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/util:ipaddr",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "socket_util_test",
    srcs = ["socket_util_test.cc"],
    shard_count = 4,
    deps = [
        ":socket_util",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/util:test_param",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "socket_base",
    srcs = ["socket_base.cc"],
    hdrs = ["socket_base.h"],
    deps = [
        ":socket_util",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "socket_base_test",
    srcs = ["socket_base_test.cc"],
    shard_count = 4,
    deps = [
        ":socket_tcp",
        ":socket_udp",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/util:test_param",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "socket_tcp",
    srcs = ["socket_tcp.cc"],
    hdrs = ["socket_tcp.h"],
    deps = [
        ":socket_base",
        ":socket_util",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "socket_tcp_test",
    srcs = ["socket_tcp_test.cc"],
    shard_count = 2,
    deps = [
        ":socket_tcp",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/util:test_param",
        "//peregrine/src/internal/util:test_util",
        "//peregrine/src/util",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "socket_udp",
    srcs = ["socket_udp.cc"],
    hdrs = ["socket_udp.h"],
    deps = [
        ":socket_base",
        ":socket_util",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "socket_udp_test",
    srcs = ["socket_udp_test.cc"],
    shard_count = 2,
    deps = [
        ":socket_udp",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/util:test_param",
        "//peregrine/src/internal/util:test_util",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "socket_test_util",
    testonly = True,
    srcs = ["socket_test_util.cc"],
    hdrs = ["socket_test_util.h"],
    deps = [
        ":socket_tcp",
        ":socket_udp",
        ":tcp_manager",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/util:test_util",
        "//peregrine/src/util:nic",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "tcp_socket_util",
    srcs = ["tcp_socket_util.cc"],
    hdrs = ["tcp_socket_util.h"],
    deps = [
        ":socket_util",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "tcp_socket_util_test",
    srcs = ["tcp_socket_util_test.cc"],
    shard_count = 2,
    deps = [
        ":socket_tcp",
        ":tcp_socket_util",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/util:test_param",
        "//peregrine/src/internal/util:test_util",
        "//peregrine/src/util",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "tcp_manager",
    srcs = ["tcp_manager.cc"],
    hdrs = ["tcp_manager.h"],
    deps = [
        ":socket_tcp",
        ":socket_util",
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/base:nicinfo",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/event:poller",
        "//peregrine/src/util:nic",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "tcp_manager_test",
    srcs = ["tcp_manager_test.cc"],
    shard_count = 4,
    deps = [
        ":socket_tcp",
        ":tcp_manager",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/base:nicinfo",
        "//peregrine/src/internal/util:test_param",
        "//peregrine/src/internal/util:test_util",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "tcp_psp_manager",
    srcs = ["tcp_psp_manager.cc"],
    hdrs = ["tcp_psp_manager.h"],
    deps = [
        ":socket_tcp",
        ":tcp_manager",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/socket/psp",
        "//peregrine/src/internal/socket/psp:psp_util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "tcp_psp_manager_test",
    srcs = ["tcp_psp_manager_test.cc"],
    shard_count = 4,
    deps = [
        ":socket_tcp",
        ":tcp_manager",
        ":tcp_psp_manager",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/base:nicinfo",
        "//peregrine/src/internal/socket/psp",
        "//peregrine/src/internal/socket/psp:psp_mock",
        "//peregrine/src/internal/socket/psp:psp_util",
        "//peregrine/src/internal/util:test_param",
        "//peregrine/src/internal/util:test_util",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)
