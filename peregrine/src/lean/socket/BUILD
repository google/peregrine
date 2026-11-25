package(default_visibility = ["//visibility:private"])

# The Lean proofs are checked by `lake build` in the `lean-proofs` GitHub
# Actions job (.github/workflows/unit_tests.yml), not by Bazel.

bzl_library(
    name = "lakefile_bzl",
    srcs = ["lakefile.bzl"],
    visibility = ["//visibility:private"],
    deps = ["//third_party/lean4/rules_lean4:data"],
)
