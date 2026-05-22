load("@nanobind_bazel//:build_defs.bzl", "nanobind_extension")
load("@rules_python//python:py_test.bzl", "py_test")

package(default_visibility = [
    "//src:__subpackages__",
    "//test:__subpackages__",
])

nanobind_extension(
    name = "peregrine",
    srcs = ["binding.cc"],
    features = ["-use_header_modules"],
    deps = [
        "//src/api:transport",
        "//src/api:transport_util",
        "//src/api:types",
    ],
)

py_test(
    name = "binding_test",
    srcs = ["binding_test.py"],
    deps = [
        ":peregrine",
        "//testing/pybase",
    ],
)
