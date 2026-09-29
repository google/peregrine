# Modular error injection utilities for Peregrine integration tests.

load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(
    default_testonly = True,
    default_visibility = ["//peregrine/test/integration:__subpackages__"],
)

cc_library(
    name = "error_injector",
    hdrs = ["error_injector.h"],
)

cc_library(
    name = "mid_chunk_disconnect",
    srcs = ["mid_chunk_disconnect.cc"],
    hdrs = ["mid_chunk_disconnect.h"],
    deps = [
        ":error_injector",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "syscall_wrap",
    srcs = ["syscall_wrap.cc"],
    hdrs = ["syscall_wrap.h"],
    linkopts = ["-Wl,--wrap=sendmsg"],
    deps = [":error_injector"],
    alwayslink = True,
)

cc_library(
    name = "error_inject_util",
    hdrs = ["error_inject_util.h"],
    deps = [
        ":error_injector",
        ":mid_chunk_disconnect",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
    ],
)
