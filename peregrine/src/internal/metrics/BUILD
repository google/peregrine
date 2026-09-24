load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "control_metrics",
    srcs = ["control_metrics.cc"],
    hdrs = ["control_metrics.h"],
    deps = [
        "//peregrine/src/api:transport_metrics",
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/internal/lib:metric_counter",
    ],
)

cc_library(
    name = "engine_metrics",
    srcs = ["engine_metrics.cc"],
    hdrs = ["engine_metrics.h"],
    deps = [
        "//peregrine/src/api:transport_metrics",
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/internal/lib:log2_histogram",
        "//peregrine/src/internal/lib:metric_counter",
    ],
)
