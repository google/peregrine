load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "tcp_psp_helper",
    srcs = ["tcp_psp_helper.cc"],
    hdrs = ["tcp_psp_helper.h"],
    deps = [
        "//src/internal/base:types",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "tcp_psp_helper_test",
    srcs = ["tcp_psp_helper_test.cc"],
    deps = [
        ":psp_syscall_mock",
        ":tcp_psp_helper",
        "//src/internal/base:types",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings:string_view",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "psp_syscall_mock",
    testonly = True,
    hdrs = ["psp_syscall_mock.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
    ],
)
