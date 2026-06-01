load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "util",
    srcs = ["util.cc"],
    hdrs = ["util.h"],
    deps = ["//src/internal/base:types"],
)

cc_test(
    name = "util_test",
    srcs = ["util_test.cc"],
    deps = [
        ":util",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "test_util",
    testonly = True,
    srcs = ["test_util.cc"],
    hdrs = ["test_util.h"],
    deps = [
        "//src/internal/base:endpoint",
        "//src/internal/base:ipaddr",
        "//src/internal/base:types",
        "//src/internal/socket:socket_tcp",
        "//src/internal/socket:socket_udp",
        "//src/util",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "test_util_test",
    srcs = ["test_util_test.cc"],
    deps = [
        ":test_util",
        "//src/internal/base:endpoint",
        "@googletest//:gtest_main",
    ],
)
