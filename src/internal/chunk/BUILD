load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("//third_party/flatbuffers:flatbuffers.bzl", "cc_flatbuffers_library", "flatbuffers_library")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "chunk",
    srcs = ["chunk.cc"],
    hdrs = ["chunk.h"],
    deps = [
        "//src/api:types",
        "//src/internal:assumptions",
        "//src/internal/base:types",
        "//third_party/gloop/util/intops",
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
        "//src/api:types",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest_main",
    ],
)

flatbuffers_library(
    name = "chunk_fbs",
    srcs = ["chunk.fbs"],
)

cc_flatbuffers_library(
    name = "chunk_cc_fbs",
    deps = [":chunk_fbs"],
)

cc_library(
    name = "chunk_flatbuf",
    srcs = ["chunk_flatbuf.cc"],
    hdrs = ["chunk_flatbuf.h"],
    deps = [
        ":chunk",
        ":chunk_cc_fbs",
        "//src/api:types",
        "//src/internal:assumptions",
        "//third_party/flatbuffers:runtime_cc",
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
    name = "tracker",
    hdrs = ["tracker.h"],
    deps = [":chunk"],
)

cc_library(
    name = "chunk_counter",
    hdrs = ["chunk_counter.h"],
    deps = [
        ":chunk",
        ":tracker",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "chunk_counter_test",
    srcs = ["chunk_counter_test.cc"],
    deps = [
        ":chunk",
        ":chunk_counter",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "chunk_tracker",
    srcs = ["chunk_tracker.cc"],
    hdrs = ["chunk_tracker.h"],
    deps = [
        ":chunk",
        ":tracker",
        "//src/internal/lib:bitset",
        "//src/util:macro",
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
        "//src/util",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
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
        "//src/api:types",
        "//src/internal:assumptions",
        "//src/internal/base:types",
        "//src/util",
        "@abseil-cpp//absl/random",
    ],
)
