load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "psp",
    hdrs = ["psp.h"],
    deps = [
        "//src/api:strong_int",
        "@abseil-cpp//absl/strings",
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

cc_library(
    name = "psp_util",
    srcs = ["psp_util.cc"],
    hdrs = ["psp_util.h"],
    deps = [
        ":psp",
        "//src/internal/base:types",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "psp_util_test",
    srcs = ["psp_util_test.cc"],
    deps = [
        ":psp",  # copybara:strip(psp)
        ":psp_mock",  # copybara:strip(psp)
        ":psp_util",
        "//src/internal/base:types",  # copybara:strip(psp)
        "@abseil-cpp//absl/log:check",  # copybara:strip(psp)
        "@abseil-cpp//absl/status",  # copybara:strip(psp)
        "@abseil-cpp//absl/status:status_matchers",  # copybara:strip(psp)
        "@abseil-cpp//absl/strings:string_view",  # copybara:strip(psp)
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "psp_mock",
    testonly = True,
    hdrs = ["psp_mock.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",  # copybara:strip(psp)
        "@abseil-cpp//absl/container:flat_hash_map",  # copybara:strip(psp)
        "@abseil-cpp//absl/memory",  # copybara:strip(psp)
        "@abseil-cpp//absl/status",  # copybara:strip(psp)
        "@abseil-cpp//absl/status:statusor",  # copybara:strip(psp)
        "@abseil-cpp//absl/synchronization",  # copybara:strip(psp)
    ],
)
