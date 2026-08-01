# For integration tests only.
# Unit tests should be put in `/peregrine/src`.

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(
    default_testonly = True,
    default_visibility = ["//visibility:private"],
)

cc_library(
    name = "flags",
    srcs = ["flags.cc"],
    hdrs = ["flags.h"],
    deps = [
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "integration",
    srcs = ["integration.cc"],
    hdrs = [
        "integration.h",
        "settings.h",
    ],
    deps = [
        ":flags",
        "//src/api:transport",
        "//src/api:transport_types",
        "//src/util:app",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "display",
    srcs = ["display.cc"],
    hdrs = ["display.h"],
    deps = [
        ":integration",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "runner",
    hdrs = ["runner.h"],
    deps = [
        "@abseil-cpp//absl/time",
    ],
)

cc_binary(
    name = "main",
    srcs = ["main.cc"],
    deps = [
        ":display",
        ":integration",
        ":runner",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log:initialize",
    ],
)
