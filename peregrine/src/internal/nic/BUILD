load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "pci_topology",
    srcs = ["pci_topology.cc"],
    hdrs = ["pci_topology.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "pci_topology_test",
    srcs = ["pci_topology_test.cc"],
    deps = [
        ":pci_topology",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "nic_inventory",
    srcs = ["nic_inventory.cc"],
    hdrs = ["nic_inventory.h"],
    deps = [
        ":pci_topology",
        "//:libibverbs",  # buildcleaner: keep
        "//peregrine/src/util:ipaddr",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "nic_inventory_test",
    srcs = ["nic_inventory_test.cc"],
    deps = [
        ":nic_inventory",
        "//peregrine/src/util:ipaddr",
        "@googletest//:gtest_main",
    ],
)
