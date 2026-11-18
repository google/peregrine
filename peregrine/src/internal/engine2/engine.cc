#include "peregrine/src/internal/engine2/engine.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/engine2/assumptions.h"
#include "peregrine/src/internal/trace/chrome_trace_exporter.h"

namespace peregrine::internal::engine2 {

bool Engine::Item::IsValid() const {
  return !requests.empty() &&
         std::all_of(requests.begin(), requests.end(),
                     [](const Request& r) { return r.IsValid(); });
}

std::unique_ptr<Engine> Engine::Create(
    const Config& config, HostInfo& self,
    GetPeerHostInfoFn get_peer_host_info_fn,
    RecvPostCallback recv_post_callback,
    ResolveAddrCallback resolve_addr_callback) {
  if ABSL_PREDICT_FALSE (!config.IsValid()) {
    LOG(ERROR) << "invalid config";
    return nullptr;
  }
  return absl::WrapUnique(new Engine(config, self));
}

Engine::Engine(const Config& config, const HostInfo& self)
    : config_(config),
      self_(self),
      tracer_(TraceConfig{.sample_every_n = 0}) {}

absl::StatusOr<Handle> Engine::Enqueue(const Endpoint& peer, Item item) {
  DCHECK(peer.HasNonzeroIpPort());
  DCHECK(item.IsValid());
  return absl::UnimplementedError("Engine::Enqueue");
}

absl::StatusOr<Status> Engine::QueryUpdate(Handle handle) {
  return absl::UnimplementedError("Engine::QueryUpdate");
}

absl::Status Engine::RegisterMemory(void* addr, size_t length) {
  return absl::OkStatus();
}

absl::Status Engine::UnregisterMemory(const void* addr) {
  return absl::OkStatus();
}

std::string Engine::ExportChromeTraceJson() const {
  static_assert(
      ::peregrine::assumptions::kRulesToFollowWhenAddingNewTraceEvents);
  static_assert(assumptions::kLocalIdentityIsReadOnlyAfterControlPlaneStarts);
  return ChromeTraceExporter::Export(tracer_.Buffer().Snapshot(),
                                     self_.control_plane_listener.ToString());
}

}  // namespace peregrine::internal::engine2
