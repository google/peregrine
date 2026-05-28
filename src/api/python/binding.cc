#include <cstdint>
#include <stdexcept>
#include <string_view>

#include "nanobind/nanobind.h"
#include "nanobind/stl/string.h"
#include "nanobind/stl/string_view.h"
#include "nanobind/stl/unique_ptr.h"
#include "src/api/transport.h"
#include "src/api/transport_util.h"
#include "src/api/types.h"

namespace peregrine {
namespace {

NB_MODULE(peregrine, m) {
  namespace nb = nanobind;
  using namespace nb::literals;  // NOLINT

  // Bind `Handle`
  nb::class_<Handle>(m, "Handle")
      .def(nb::init<uint32_t>())
      .def("value", [](const Handle& h) { return h.value(); });

  // Bind `Buffer`
  nb::class_<Buffer>(m, "Buffer")
      .def(nb::init<uint32_t>())
      .def("value", [](const Buffer& b) { return b.value(); });

  // Bind `Op` enum
  nb::enum_<Op>(m, "Op").value("READ", Op::kRead).value("WRITE", Op::kWrite);

  // Bind `Status` enum and helper functions
  nb::enum_<Status>(m, "Status")
      .value("IN_PROGRESS", Status::kInProgress)
      .value("SUCCESS", Status::kSuccess)
      .value("FAILURE", Status::kFailure);

  m.def("is_in_progress", &IsInProgress);
  m.def("is_completed", &IsCompleted);

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
          [](Transport& t, std::string_view peer, const Request& request) {
            if (const auto result = t.Post(peer, request); !result.ok()) {
              throw std::runtime_error(result.status().ToString());
            } else {
              return result.value();
            }
          },
          nb::arg("peer"), nb::arg("request"))
      .def(
          "poll",
          [](Transport& t, Handle handle) {
            if (const auto result = t.Poll(handle); !result.ok()) {
              throw std::runtime_error(result.status().ToString());
            } else {
              return result.value();
            }
          },
          nb::arg("handle"));

  m.def("create_transport", &CreateTransport);
}

}  // namespace
}  // namespace peregrine
