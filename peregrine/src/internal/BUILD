# Only for internal implementations.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package_group(
    name = "public_api",
    packages = ["//peregrine/src/api/..."],
)

package_group(
    name = "internal_impl",
    includes = [":public_api"],
    packages = ["//peregrine/src/internal/..."],
)

package(default_visibility = [
    # Do _not_ add anything new here.
    # We want `peregrine/src/internal` to be free to change
    # as long as it doesn't break `peregrine/src/api`.
    "//peregrine/src/api:__pkg__",
])

cc_library(
    name = "assumptions",
    hdrs = ["assumptions.h"],
    visibility = ["//peregrine/src/internal:__subpackages__"],
)

cc_library(
    name = "coding_style",
    hdrs = ["coding_style.h"],
    visibility = ["//peregrine/src/internal:__subpackages__"],
)

cc_library(
    name = "transport_impl",
    srcs = ["transport_impl.cc"],
    hdrs = ["transport_impl.h"],
    deps = [
        ":assumptions",
        "//peregrine/src/api:transport",
        "//peregrine/src/api:transport_metrics",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:config",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:hostinfo",
        "//peregrine/src/internal/control",
        "//peregrine/src/internal/engine",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "transport_impl_test",
    srcs = ["transport_impl_test.cc"],
    shard_count = 9,
    deps = [
        "//peregrine/src/api:transport",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/util:app",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)
