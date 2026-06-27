load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

package(
    default_testonly = True,
    default_visibility = ["//visibility:private"],
)

cc_binary(
    name = "pg_2node",
    srcs = ["pg_2node.cc"],
    deps = [
        "//src/api:transport",
        "//src/api:transport_types",
        "//src/api:transport_util",
        "//src/util",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)
