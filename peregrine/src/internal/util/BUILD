load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "util",
    srcs = ["util.cc"],
    hdrs = ["util.h"],
    deps = [
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:types",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "util_test",
    srcs = ["util_test.cc"],
    deps = [
        ":util",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "test_iov",
    testonly = True,
    srcs = ["test_iov.cc"],
    hdrs = ["test_iov.h"],
    deps = [
        ":util",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:types",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "test_iov_test",
    srcs = ["test_iov_test.cc"],
    deps = [
        ":test_iov",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:types",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "test_param",
    testonly = True,
    srcs = ["test_param.cc"],
    hdrs = ["test_param.h"],
    deps = [
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "test_util",
    testonly = True,
    srcs = ["test_util.cc"],
    hdrs = ["test_util.h"],
    deps = [
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/socket:socket_tcp",
        "//peregrine/src/internal/socket:socket_udp",
        "//peregrine/src/util",
        "//peregrine/src/util:ipaddr",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "test_util_test",
    srcs = ["test_util_test.cc"],
    deps = [
        ":test_util",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)
