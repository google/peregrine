# Only for internal implementations.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = [
    # Do _not_ add anything new here.
    # We want `peregrine/src/internal` to be free to change
    # as long as it doesn't break `peregrine/src/api`.
    "//src/api:__pkg__",
])

cc_library(
    name = "assumptions",
    hdrs = ["assumptions.h"],
)

cc_library(
    name = "coding_style",
    hdrs = ["coding_style.h"],
)

cc_library(
    name = "transport_impl",
    hdrs = ["transport_impl.h"],
    deps = [
        ":engine/engine",
        "//src/api:transport",
        "//src/api:types",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "transport_impl_test",
    srcs = ["transport_impl_test.cc"],
    deps = [
        ":transport_impl",
        "//src/api:transport",
        "//src/api:types",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "engine/engine",
    srcs = ["engine/engine.cc"],
    hdrs = ["engine/engine.h"],
    deps = [
        ":assumptions",
        ":coding_style",
        "//src/api:types",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
    ],
)
