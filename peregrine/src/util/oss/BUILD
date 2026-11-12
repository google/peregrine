load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/util:__pkg__"])

cc_library(
    name = "nic_filter",
    srcs = ["nic_filter.cc"],
    hdrs = ["//peregrine/src/util:nic_filter.h"],
)

cc_test(
    name = "nic_filter_test",
    srcs = ["nic_filter_test.cc"],
    deps = [
        ":nic_filter",
        "@googletest//:gtest_main",
    ],
)
