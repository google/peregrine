#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "nanobind/nanobind.h"
#include "nanobind/stl/string.h"
#include "nanobind/stl/string_view.h"
#include "nanobind/stl/unique_ptr.h"
#include "nanobind/stl/vector.h"
#include "src/api/transport.h"
#include "src/api/transport_types.h"
#include "src/api/transport_util.h"

namespace {
// Translates Abseil errors into Python exceptions.
void ThrowIfFailed(const absl::Status& status) {
  if (!status.ok()) {
    throw std::runtime_error(status.ToString());
  }
}
}  // namespace

NB_MODULE(peregrine, m) {
  namespace nb = nanobind;
  using namespace nb::literals;  // NOLINT
  using namespace peregrine;     // NOLINT

  // Bind `Handle`
  nb::class_<Handle>(m, "Handle")
      .def(nb::init<uint32_t>())
      .def("value", [](const Handle& h) { return h.value(); });

  // Bind `Op` enum
  nb::enum_<Op>(m, "Op").value("READ", Op::kRead).value("WRITE", Op::kWrite);

  // Bind `Request` struct
  nb::class_<Request>(m, "Request")
      .def(nb::init<>())
      .def(
          "__init__",
          [](Request* r, Op op, uintptr_t laddr, uintptr_t raddr, size_t len) {
            new (r) Request{.op = op,
                            .laddr = reinterpret_cast<Byte*>(laddr),
                            .raddr = reinterpret_cast<Byte*>(raddr),
                            .len = len};
          },
          "op"_a, "laddr"_a, "raddr"_a, "len"_a)
      .def_rw("op", &Request::op)
      .def_prop_rw(
          "laddr",
          [](const Request& r) { return reinterpret_cast<uintptr_t>(r.laddr); },
          [](Request& r, uintptr_t addr) {
            r.laddr = reinterpret_cast<Byte*>(addr);
          })
      .def_prop_rw(
          "raddr",
          [](const Request& r) { return reinterpret_cast<uintptr_t>(r.raddr); },
          [](Request& r, uintptr_t addr) {
            r.raddr = reinterpret_cast<Byte*>(addr);
          })
      .def_rw("len", &Request::len)
      .def("is_valid", &Request::IsValid)
      .def("__str__", &Request::ToString)
      .def("__repr__", &Request::ToString);

  // Bind `Transport` interface and functions
  nb::class_<Transport>(m, "Transport")
      .def(
          "post",
          [](Transport& self, std::string_view peer,
             const std::vector<Request>& requests) -> Handle {
            const auto reqs = absl::MakeConstSpan(requests);
            const absl::StatusOr<Handle> handle = self.Post(peer, reqs);
            ThrowIfFailed(handle.status());
            return handle.value();
          },
          nb::arg("peer"), nb::arg("requests"))
      .def(
          "poll",
          [](Transport& t, Handle handle) {
            const absl::StatusOr<peregrine::Status> status = t.Poll(handle);
            ThrowIfFailed(status.status());
            return status.value();
          },
          nb::arg("handle"));

  m.def("create_transport", &CreateTransport, nb::arg("endpoints"),
        nb::arg("num_conns_per_peer") = 8);

  // Bind `Status` enum and helper functions
  nb::enum_<Status>(m, "Status")
      .value("IN_PROGRESS", Status::kInProgress)
      .value("SUCCESS", Status::kSuccess)
      .value("FAILURE", Status::kFailure);

  m.def("is_in_progress", &IsInProgress);
  m.def("is_completed", &IsCompleted);
}

#if defined(__has_feature) && __has_feature(dataflow_sanitizer)
asm(".globl PyInit_peregrine\n"
    "PyInit_peregrine = PyInit_peregrine.dfsan");
#endif
