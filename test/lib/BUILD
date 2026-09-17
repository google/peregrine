# For test libraries only.

load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(
    default_testonly = True,
    default_visibility = ["//test:__subpackages__"],
)

cc_library(
    name = "initialization",
    hdrs = ["initialization.h"],
    deps = [
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log:initialize",
    ],
)
