# Peregrine: A Transport Library for ML Workloads on TPUs

Our goal is to build an easy-to-use, light-weight, high-performance, and general
transport library to serve machine learning workloads on TPU hosts.

## Project Status

- Peregrine is still in experimental mode.
- You must contact us to get our support.

## Code Organization

- `peregrine/src/`: the library and all the unit tests
- `peregrine/test/`: the integration tests and benchmarks
- `ci/`: continuous integration in GitHub

## Quick Start

- `run.sh` builds and tests all the code.

- To start the integration test, run

```
  $ bazelisk run //peregrine/test/integration:main
```
