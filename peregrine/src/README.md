# Code Organization

- This `/peregrine/src/` folder contains src code and all the unit tests.
- The integration tests are in the `/peregrine/test/` folder.

| Subdirectory |        Namespace        |         Purpose             |
| :----------- | :---------------------- | :-------------------------- |
| `api/`       | `peregrine::`           | Public-facing API           |
| `util/`      | `peregrine::util::`     | Internally-public utilities |
| `internal/`  | `peregrine::internal::` | Internal implementations    |
