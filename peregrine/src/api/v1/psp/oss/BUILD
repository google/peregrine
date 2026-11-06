load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "psp",
    srcs = ["psp.cc"],
    hdrs = ["//peregrine/src/api/v1:psp/psp.h"],
    deps = [
        "//peregrine/src/internal/socket/psp",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@grpc//:grpc++",
    ],
)

cc_test(
    name = "psp_test",
    srcs = ["psp_test.cc"],
    deps = [
        ":psp",
        "@googletest//:gtest_main",
    ],
)
