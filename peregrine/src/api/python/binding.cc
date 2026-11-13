#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "nanobind/nanobind.h"
#include "nanobind/stl/array.h"
#include "nanobind/stl/optional.h"
#include "nanobind/stl/string.h"
#include "nanobind/stl/string_view.h"
#include "nanobind/stl/unique_ptr.h"
#include "nanobind/stl/vector.h"
#include "peregrine/src/api/transport.h"
#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/api/transport_util.h"

namespace {
// Translates Abseil errors into Python exceptions.
void ThrowIfFailed(const absl::Status& status) {
  if (!status.ok()) {
    throw std::runtime_error(status.ToString());
  }
}

// Universal type that can implicitly convert to any type.
struct AnyType {
  template <typename T>
  operator T();
};

// Counts the number of fields in a struct at compile time.
template <typename T>
consteval size_t CountFields(auto... args) {
  if constexpr (requires { T{args...}; }) {
    return CountFields<T>(args..., AnyType{});
  } else {
    return sizeof...(args) - 1;
  }
}

// Releases the GIL during `Transport` teardown so worker threads running Python
// callbacks do not deadlock.
class PyTransport final : public peregrine::Transport {
 public:
  PyTransport(std::unique_ptr<peregrine::Transport> inner,
              std::shared_ptr<nanobind::callable> resolve_cb)
      : inner_(std::move(inner)), resolve_cb_(std::move(resolve_cb)) {}

  ~PyTransport() override {
    nanobind::gil_scoped_release release;
    inner_.reset();
  }

  std::string GetControlEndpoint() const override {
    return inner_->GetControlEndpoint();
  }

  absl::StatusOr<peregrine::Handle> Post(
      std::string_view peer, absl::Span<const peregrine::Request> requests,
      absl::AnyInvocable<void(peregrine::Status)> on_complete,
      const peregrine::Metadata& metadata) override {
    return inner_->Post(peer, requests, std::move(on_complete), metadata);
  }

  absl::StatusOr<peregrine::Status> Poll(peregrine::Handle handle) override {
    return inner_->Poll(handle);
  }

  absl::Status RegisterMemory(void* addr, size_t length) override {
    return inner_->RegisterMemory(addr, length);
  }

  absl::Status UnregisterMemory(const void* addr) override {
    return inner_->UnregisterMemory(addr);
  }

  peregrine::TransportMetrics GetTransportMetrics() const override {
    return inner_->GetTransportMetrics();
  }

  void ConfigureTrace(const peregrine::TraceConfig& config) override {
    inner_->ConfigureTrace(config);
  }

  void ClearTrace() override { inner_->ClearTrace(); }

  std::string ExportChromeTraceJson() const override {
    return inner_->ExportChromeTraceJson();
  }

 private:
  std::unique_ptr<peregrine::Transport> inner_;
  std::shared_ptr<nanobind::callable> resolve_cb_;
};
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
      .def("get_control_endpoint", &Transport::GetControlEndpoint)
      .def(
          "post",
          [](Transport& self, std::string_view peer,
             const std::vector<Request>& requests) -> Handle {
            const auto reqs = absl::MakeConstSpan(requests);
            const absl::StatusOr<Handle> handle = self.Post(peer, reqs);
            ThrowIfFailed(handle.status());
            return handle.value();
          },
          nb::arg("peer"), nb::arg("requests"),
          nb::call_guard<nb::gil_scoped_release>())
      .def(
          "poll",
          [](Transport& t, Handle handle) {
            const absl::StatusOr<peregrine::Status> status = t.Poll(handle);
            ThrowIfFailed(status.status());
            return status.value();
          },
          nb::arg("handle"), nb::call_guard<nb::gil_scoped_release>())
      .def(
          "register_memory",
          [](Transport& self, uintptr_t addr, size_t length) {
            ThrowIfFailed(
                self.RegisterMemory(reinterpret_cast<void*>(addr), length));
          },
          nb::arg("addr"), nb::arg("length"))
      .def(
          "unregister_memory",
          [](Transport& self, uintptr_t addr) {
            ThrowIfFailed(
                self.UnregisterMemory(reinterpret_cast<const void*>(addr)));
          },
          nb::arg("addr"))
      .def("get_transport_metrics", &Transport::GetTransportMetrics)
      .def("configure_trace", &Transport::ConfigureTrace, nb::arg("config"),
           "Dynamically updates the trace configuration.")
      .def("clear_trace", &Transport::ClearTrace,
           "Clears all buffered Post() trace records.")
      .def("export_chrome_trace_json", &Transport::ExportChromeTraceJson,
           "Exports Post() trace records in Chrome Trace Event JSON format.");

  // Bind TraceConfig
  constexpr size_t kTraceConfigFieldsCount = 1;
  nb::class_<TraceConfig>(m, "TraceConfig")
      .def(nb::init<>())
      .def(
          "__init__",
          [](TraceConfig* c, uint32_t sample_every_n) {
            new (c) TraceConfig{
                .sample_every_n = sample_every_n,
            };
          },
          nb::arg("sample_every_n") = 0)
      .def_rw("sample_every_n", &TraceConfig::sample_every_n);
  static_assert(CountFields<peregrine::TraceConfig>() ==
                kTraceConfigFieldsCount);

  // Bind `Log2Histogram<32>`
  nb::class_<Log2Histogram<32>>(m, "Log2Histogram32")
      .def_ro("sum", &Log2Histogram<32>::sum)
      .def_ro("buckets", &Log2Histogram<32>::buckets)
      .def("num_buckets", &Log2Histogram<32>::NumBuckets)
      .def("count", &Log2Histogram<32>::Count)
      .def("__str__", &Log2Histogram<32>::ToString)
      .def("__repr__", &Log2Histogram<32>::ToString);

  // Bind `OpMetrics`
  constexpr size_t kOpMetricsFieldsCount = 4;
  nb::class_<OpMetrics>(m, "OpMetrics")
      .def_ro("e2e_latency_us", &OpMetrics::e2e_latency_us)
      .def_ro("request_size_bytes", &OpMetrics::request_size_bytes)
      .def_ro("bytes", &OpMetrics::bytes)
      .def_ro("errors", &OpMetrics::errors);
  static_assert(CountFields<peregrine::OpMetrics>() == kOpMetricsFieldsCount);

  // Bind `TransportMetrics`
  // When adding new metrics, increment the count and update the binding.
  constexpr size_t kTransportMetricsFieldsCount = 5;
  nb::class_<TransportMetrics>(m, "TransportMetrics")
      .def_ro("write", &TransportMetrics::write)
      .def_ro("read", &TransportMetrics::read)
      .def_ro("recv_post_latency_us", &TransportMetrics::recv_post_latency_us)
      .def_ro("tcp_connect_failures", &TransportMetrics::tcp_connect_failures)
      .def_ro("rpc_requests_received",
              &TransportMetrics::rpc_requests_received);
  static_assert(CountFields<peregrine::TransportMetrics>() ==
                kTransportMetricsFieldsCount);

  // Bind `TransportType` enum
  nb::enum_<TransportType>(m, "TransportType")
      .value("ANY", TransportType::kAny)
      .value("TCP", TransportType::kTcp)
      .value("RDMA", TransportType::kRdma);

  m.def(
      "create_transport",
      [](std::string_view control_endpoint, TransportType transport_type,
         int num_conns_per_peer,
         std::optional<nb::callable> resolve_addr_callback)
          -> std::unique_ptr<Transport> {
        Options options{
            .transport_type = transport_type,
            .num_parallel_connections = num_conns_per_peer,
        };
        std::shared_ptr<nb::callable> resolve_cb;
        if (resolve_addr_callback.has_value()) {
          resolve_cb =
              std::make_shared<nb::callable>(std::move(*resolve_addr_callback));
          nb::callable* cb_ptr = resolve_cb.get();
          options.resolve_addr_callback =
              [cb_ptr](const Metadata& post_meta,
                       const Metadata& req_meta) -> Byte* {
            nb::gil_scoped_acquire gil;
            try {
              nb::bytes post_bytes(
                  reinterpret_cast<const char*>(post_meta.data()),
                  post_meta.size());
              nb::bytes req_bytes(
                  reinterpret_cast<const char*>(req_meta.data()),
                  req_meta.size());
              nb::object result = (*cb_ptr)(post_bytes, req_bytes);
              if (result.is_none()) {
                return nullptr;
              }
              uintptr_t addr = nb::cast<uintptr_t>(result);
              return reinterpret_cast<Byte*>(addr);
            } catch (nb::python_error& e) {
              e.discard_as_unraisable(cb_ptr->ptr());
              return nullptr;
            } catch (const std::exception&) {
              return nullptr;
            }
          };
        }
        std::unique_ptr<Transport> inner;
        {
          nb::gil_scoped_release release;
          inner = CreateTransport(control_endpoint, std::move(options));
        }
        if (inner == nullptr) {
          return nullptr;
        }
        return std::make_unique<PyTransport>(std::move(inner),
                                             std::move(resolve_cb));
      },
      nb::arg("control_endpoint"),
      nb::arg("transport_type") = TransportType::kTcp,
      nb::arg("num_conns_per_peer") = 8,
      nb::arg("resolve_addr_callback") = nb::none());

  // Bind `Status` enum and helper functions
  nb::enum_<Status>(m, "Status")
      .value("IN_PROGRESS", Status::kInProgress)
      .value("SUCCESS", Status::kSuccess)
      .value("FAILURE", Status::kFailure);

  m.def("is_in_progress", &IsInProgress);
  m.def("is_completed", &IsCompleted);
}

#if defined(__has_feature)
#if __has_feature(dataflow_sanitizer)
asm(".globl PyInit_peregrine\n"
    "PyInit_peregrine = PyInit_peregrine.dfsan");
#endif
#endif
