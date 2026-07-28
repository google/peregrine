# Integration Tests

- This `/peregrine/test/` folder contains integration tests and benchmarks;
- Unit tests should be put in the `/peregrine/src/` folder together
  with the `.{h,cc}` files they target to test. This way, it is easy to
  see if there is any unit test missing.

| Subdirectory   |           Namespace          |         Purpose            |
| :------------- | :--------------------------- | :------------------------- |
| `benchmark/`   | `peregrine::benchmark`       | Benchmarks across hosts.   |
| `integration/` | `peregrine::integration`     | Tests in a single process. |