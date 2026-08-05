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
    name = "settings",
    hdrs = ["settings.h"],
    deps = [
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "controlpath-host",
    srcs = ["controlpath-host.cc"],
    hdrs = ["controlpath-host.h"],
    deps = [
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "datapath-host",
    srcs = ["datapath-host.cc"],
    hdrs = ["datapath-host.h"],
    deps = [
        "//src/api:transport",
        "//src/api:transport_types",
        "//src/api:transport_util",
        "//src/util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "ncurses",
    srcs = ["ncurses.cc"],
    hdrs = ["ncurses.h"],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@ncurses",
    ],
)

cc_library(
    name = "integration",
    srcs = ["integration.cc"],
    hdrs = [
        "integration.h",
    ],
    deps = [
        ":controlpath-host",
        ":datapath-host",
        ":flags",
        ":settings",
        "//src/api:transport_types",
        "//src/util",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "display",
    srcs = ["display.cc"],
    hdrs = ["display.h"],
    deps = [
        ":flags",
        ":integration",
        ":ncurses",
        ":settings",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
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
