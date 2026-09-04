load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "engine_metrics",
    hdrs = ["engine_metrics.h"],
    deps = [
        "//src/api:transport_types",
        "//src/internal:assumptions",
        "//src/internal/lib:metric_counter",
    ],
)
