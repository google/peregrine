# Python orchestrator and CLI for Peregrine multi-node cluster benchmarks.

load("@rules_python//python:py_binary.bzl", "py_binary")
load("@rules_python//python:py_library.bzl", "py_library")
load("@rules_python//python:py_test.bzl", "py_test")

package(
    default_testonly = True,
    default_visibility = ["//peregrine/test:__subpackages__"],
)

py_library(
    name = "preflight_py",
    srcs = ["preflight.py"],
    deps = ["@abseil-py//absl/logging"],
)

py_test(
    name = "preflight_test",
    srcs = ["preflight_test.py"],
    main = "preflight_test.py",
    deps = [
        ":preflight_py",
        "@abseil-py//absl/testing:absltest",
    ],
)

py_library(
    name = "node_executor_py",
    srcs = ["node_executor.py"],
    deps = [
        ":preflight_py",
        "@abseil-py//absl/logging",
    ],
)

py_test(
    name = "node_executor_test",
    srcs = ["node_executor_test.py"],
    main = "node_executor_test.py",
    deps = [
        ":node_executor_py",
        "@abseil-py//absl/testing:absltest",
    ],
)

py_library(
    name = "report_py",
    srcs = ["report.py"],
)

py_test(
    name = "report_test",
    srcs = ["report_test.py"],
    main = "report_test.py",
    deps = [
        ":report_py",
        "@abseil-py//absl/testing:absltest",
    ],
)

py_library(
    name = "cluster_session_py",
    srcs = ["cluster_session.py"],
    data = ["//peregrine/test/cluster/node"],
    deps = [
        ":node_executor_py",
        ":preflight_py",
        ":report_py",
        "@abseil-py//absl/logging",
    ],
)

py_binary(
    name = "bipartite_runner",
    srcs = ["bipartite_runner.py"],
    data = ["//peregrine/test/cluster/node"],
    main = "bipartite_runner.py",
    deps = [
        ":cluster_session_py",
        ":node_executor_py",
        ":report_py",
        "@abseil-py//absl:app",
        "@abseil-py//absl/flags",
        "@abseil-py//absl/logging",
    ],
)

py_test(
    name = "bipartite_runner_test",
    srcs = ["bipartite_runner_test.py"],
    data = ["//peregrine/test/cluster/node"],
    main = "bipartite_runner_test.py",
    deps = [
        ":cluster_session_py",
        "//peregrine/src/util:util_py",
        "@abseil-py//absl/testing:absltest",
    ],
)
