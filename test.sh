#!/bin/bash

set -e

bazelisk build //
bazelisk test //
