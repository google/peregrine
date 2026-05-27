load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "ip_util",
    srcs = ["ip_util.cc"],
    hdrs = ["ip_util.h"],
    deps = [
        "//src/internal/base:types",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "ip_util_test",
    srcs = ["ip_util_test.cc"],
    deps = [
        ":ip_util",
        "//src/internal/util:test_util",
        "@abseil-cpp//absl/strings:string_view",
        "@googletest//:gtest_main",
    ],
)
