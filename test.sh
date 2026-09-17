#!/bin/bash

set -e

bazelisk build //...
bazelisk test //...

# If `MODULE.bazel.lock` is updated, check it in.
