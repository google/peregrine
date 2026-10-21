load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "psp_mock",
    testonly = True,
    hdrs = ["psp_mock.h"],
)

cc_library(
    name = "psp_util",
    srcs = ["psp_util.cc"],
    hdrs = ["//peregrine/src/internal/socket/psp:psp_util.h"],
    deps = [
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/socket/psp",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "psp_util_test",
    srcs = ["psp_util_test.cc"],
    deps = [
        ":psp_util",
        "@googletest//:gtest_main",
    ],
)
