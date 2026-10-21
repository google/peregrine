load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "chunk_trace_wire",
    hdrs = ["chunk_trace_wire.h"],
)

cc_library(
    name = "trace_event",
    hdrs = ["trace_event.h"],
)

cc_library(
    name = "trace_sampler",
    hdrs = ["trace_sampler.h"],
    deps = [
        "//peregrine/src/internal:coding_style",
        "//peregrine/src/util:macro",
    ],
)

cc_test(
    name = "trace_sampler_test",
    srcs = ["trace_sampler_test.cc"],
    deps = [
        ":trace_sampler",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "trace_buffer",
    srcs = ["trace_buffer.cc"],
    hdrs = ["trace_buffer.h"],
    deps = [
        ":trace_event",
        "//peregrine/src/internal:coding_style",
        "//peregrine/src/util:macro",
    ],
)

cc_test(
    name = "trace_buffer_test",
    srcs = ["trace_buffer_test.cc"],
    deps = [
        ":trace_buffer",
        ":trace_event",
        "//peregrine/src/util:thread",
        "@googletest//:gtest_main",
    ],
)
