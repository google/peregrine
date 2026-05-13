# Only for public API.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = [
    # TODO(yongx): make the visibility public
    "//src:__subpackages__",
    "//test:__subpackages__",
])

cc_library(
    name = "types",
    srcs = ["types.cc"],
    hdrs = ["types.h"],
    deps = [
        "//util/intops",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
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
    name = "transport",
    hdrs = ["transport.h"],
    deps = [
        ":types",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "transport_util",
    hdrs = ["transport_util.h"],
    deps = [
        ":transport",
        "//src/internal:transport_impl",
    ],
)

cc_test(
    name = "transport_util_test",
    srcs = ["transport_util_test.cc"],
    deps = [
        ":transport_util",
        "@googletest//:gtest_main",
    ],
)
