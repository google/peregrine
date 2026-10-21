load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "psp",
    hdrs = ["psp.h"],
    deps = [
        "//peregrine/src/util:strong_int",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "psp_test",
    srcs = ["psp_test.cc"],
    deps = [
        ":psp",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:string_view",
        "@googletest//:gtest_main",
    ],
)

exports_files(["psp_util.h"])

cc_library(
    name = "psp_util",
    hdrs = ["psp_util.h"],
    deps = [
        ":psp",
        "//peregrine/src/internal/base:types",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "psp_mock",
    testonly = True,
    hdrs = ["psp_mock.h"],
    deps = [
        "//peregrine/src/internal/socket/psp/oss:psp_mock",
    ],
)
