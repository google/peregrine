load("@flatbuffers//:build_defs.bzl", "flatbuffer_cc_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "chunk",
    srcs = ["chunk.cc"],
    hdrs = ["chunk.h"],
    deps = [
        "//peregrine/src/api:strong_int",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/internal/base:types",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "chunk_test",
    srcs = ["chunk_test.cc"],
    deps = [
        ":chunk",
        ":chunk_test_util",
        "//peregrine/src/api:transport_types",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest_main",
    ],
)

flatbuffer_cc_library(
    name = "chunk_cc_fbs",
    srcs = ["chunk.fbs"],
)

cc_library(
    name = "chunk_flatbuf",
    srcs = ["chunk_flatbuf.cc"],
    hdrs = ["chunk_flatbuf.h"],
    deps = [
        ":chunk",
        ":chunk_cc_fbs",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/internal/base:types",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "chunk_flatbuf_test",
    srcs = ["chunk_flatbuf_test.cc"],
    deps = [
        ":chunk",
        ":chunk_flatbuf",
        ":chunk_test_util",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/random",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "chunk_tracker",
    srcs = ["chunk_tracker.cc"],
    hdrs = ["chunk_tracker.h"],
    deps = [
        ":chunk",
        "//peregrine/src/internal/lib:bitset",
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "chunk_tracker_test",
    srcs = ["chunk_tracker_test.cc"],
    deps = [
        ":chunk",
        ":chunk_tracker",
        "//peregrine/src/util",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:bit_gen_ref",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "chunk_test_util",
    testonly = True,
    srcs = ["chunk_test_util.cc"],
    hdrs = ["chunk_test_util.h"],
    deps = [
        ":chunk",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/util",
        "@abseil-cpp//absl/random:bit_gen_ref",
    ],
)
