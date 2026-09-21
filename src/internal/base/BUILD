load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "config",
    hdrs = ["config.h"],
    deps = [
        "//src/api:transport_types",
        "//src/internal:assumptions",
        "@grpc//:grpc++",
    ],
)

cc_library(
    name = "types",
    hdrs = ["types.h"],
    deps = [
        "//src/api:strong_int",
    ],
)

cc_library(
    name = "endpoint",
    srcs = ["endpoint.cc"],
    hdrs = ["endpoint.h"],
    deps = [
        ":types",
        "//src/internal:assumptions",
        "//src/util:ipaddr",
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
        "//src/util:ipaddr",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "nicinfo",
    srcs = ["nicinfo.cc"],
    hdrs = ["nicinfo.h"],
    deps = [
        ":endpoint",
        "//src/internal:assumptions",
        "//src/util:nic",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "nicinfo_test",
    srcs = ["nicinfo_test.cc"],
    deps = [
        ":endpoint",
        ":nicinfo",
        "//src/util:nic",
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
        ":nicinfo",
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
        ":nicinfo",
        "//src/util:nic",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)
