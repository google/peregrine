load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "route",
    srcs = ["route.cc"],
    hdrs = ["route.h"],
    deps = [
        "//peregrine/src/util:ipaddr",
        "//peregrine/src/util:macro",
        "//peregrine/src/util:nic",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "route_test",
    srcs = ["route_test.cc"],
    deps = [
        ":route",
        "//peregrine/src/util:ipaddr",
        "//peregrine/src/util:nic",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest_main",
    ],
)
