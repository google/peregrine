load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "control_metrics",
    hdrs = ["control_metrics.h"],
    deps = [
        "//src/api:transport_metrics",
        "//src/internal:assumptions",
        "//src/internal/lib:metric_counter",
    ],
)

cc_library(
    name = "engine_metrics",
    hdrs = ["engine_metrics.h"],
    deps = [
        "//src/api:transport_metrics",
        "//src/internal:assumptions",
        "//src/internal/lib:metric_counter",
    ],
)
