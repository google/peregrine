load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "channel",
    hdrs = ["channel.h"],
    deps = [
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
        "//src/internal/base:endpoint",
        "//src/internal/socket:acceptor",
        "//src/internal/socket:connector",
        "//src/internal/socket:socket_tcp",
        "//src/internal/socket:socket_udp",
        "//src/internal/util:test_util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "channel_tcp",
    srcs = ["channel_tcp.cc"],
    hdrs = ["channel_tcp.h"],
    deps = [
        ":channel",
        "//src/api:transport_types",
        "//src/internal/base:types",
        "//src/internal/socket:socket_tcp",
        "//src/internal/socket:socket_util",
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
    name = "channel_msg",
    testonly = True,
    srcs = ["channel_msg.cc"],
    hdrs = ["channel_msg.h"],
    deps = [
        ":channel",
        "//src/api:transport_types",
        "//src/internal/base:types",
        "//src/internal/util:test_iov",
        "//src/util",
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
        "//src/api:transport_types",
        "//src/internal/base:types",
        "//src/internal/util:test_iov",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "channel_test",
    srcs = ["channel_test.cc"],
    deps = [
        ":channel",
        ":channel_test_util",
        "//src/api:transport_types",
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
    deps = [
        ":channel_util",
        "//src/internal/base:endpoint",
        "//src/internal/socket:acceptor",
        "//src/internal/socket:socket_tcp",
        "//src/internal/util:test_util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)
