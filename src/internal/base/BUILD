load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "config",
    hdrs = ["config.h"],
    deps = ["@grpc//:grpc++"],
)

cc_library(
    name = "types",
    hdrs = ["types.h"],
    deps = [
        "//src/api:strong_int",
    ],
)

cc_library(
    name = "ipaddr",
    srcs = ["ipaddr.cc"],
    hdrs = ["ipaddr.h"],
    deps = [
        "//src/internal:assumptions",
        "//src/util:macro",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "ipaddr_test",
    srcs = ["ipaddr_test.cc"],
    deps = [
        ":ipaddr",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "endpoint",
    srcs = ["endpoint.cc"],
    hdrs = ["endpoint.h"],
    deps = [
        ":ipaddr",
        ":types",
        "//src/internal:assumptions",
        "//src/util:macro",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "endpoint_test",
    srcs = ["endpoint_test.cc"],
    deps = [
        ":endpoint",
        ":ipaddr",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "hostinfo",
    srcs = ["hostinfo.cc"],
    hdrs = ["hostinfo.h"],
    deps = [
        ":endpoint",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "hostinfo_test",
    srcs = ["hostinfo_test.cc"],
    deps = [
        ":endpoint",
        ":hostinfo",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)
