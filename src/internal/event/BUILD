load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "poller",
    srcs = ["poller.cc"],
    hdrs = ["poller.h"],
    deps = [
        "//src/internal/base:types",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "poller_test",
    srcs = ["poller_test.cc"],
    deps = [
        ":poller",
        "//src/internal/base:types",
        "@googletest//:gtest_main",
    ],
)
