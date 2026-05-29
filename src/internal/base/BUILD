load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "types",
    srcs = ["types.cc"],
    hdrs = ["types.h"],
    deps = [
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "types_test",
    srcs = ["types_test.cc"],
    deps = [
        ":types",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "endpoint",
    srcs = ["endpoint.cc"],
    hdrs = ["endpoint.h"],
    deps = [
        ":types",
        "//src/internal:assumptions",
        "//src/internal/socket:ip_util",
        "//src/util:macro",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "endpoint_test",
    srcs = ["endpoint_test.cc"],
    deps = [
        ":endpoint",
        ":types",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@googletest//:gtest_main",
    ],
)
