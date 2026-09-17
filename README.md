# Peregrine: A Transport Library for ML Workloads on TPUs

Our goal is to build an easy-to-use, light-weight, high-performance, and general
transport library to serve machine learning workloads on TPU hosts.

## Code Organization

- `src/`: the library and all the unit tests
- `test/`: integration tests and benchmarks

## Quick Start

- To build and test all the code, run

```
  $ ./test.sh
```

- To start the integration test, run

```
  $ bazelisk run //test/integration:main
```
