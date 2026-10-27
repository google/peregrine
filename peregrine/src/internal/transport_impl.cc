#include "peregrine/src/internal/transport_impl.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/control/control.h"
#include "peregrine/src/internal/engine/engine.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_recorder.h"

namespace peregrine::internal {

std::unique_ptr<TransportImpl> TransportImpl::Create(
    const Config& config, const Endpoint& endpoint, SecurityCredentials creds,
    RecvPostCallback recv_post_callback) {
  if ABSL_PREDICT_FALSE (!config.IsValid()) {
    LOG(ERROR) << "invalid config";
    return nullptr;
  }
  if ABSL_PREDICT_FALSE (endpoint.HasZeroIpAddr()) {
    LOG(ERROR) << "invalid control-plane endpoint " << endpoint;
    return nullptr;
  }
  if ABSL_PREDICT_FALSE (!creds.IsValid()) {
    LOG(ERROR) << "invalid security credentials";
    return nullptr;
  }

  // Set host info and then create control plane.
  auto t = absl::WrapUnique(new TransportImpl(config));
  t->self_.control_plane_listener = endpoint;
  t->self_.data_plane_listeners = {};
  t->control_ = Control::Create(t->config_, t->self_, creds);
  if ABSL_PREDICT_FALSE (t->control_ == nullptr) {
    LOG(ERROR) << "failed to create control plane for " << t->self_;
    return nullptr;
  }

  // Create data plane, which will populate host info.
  t->engine_ = Engine::Create(t->config_, t->self_, *t->control_,
                              std::move(recv_post_callback));
  if ABSL_PREDICT_FALSE (t->engine_ == nullptr) {
    LOG(ERROR) << "failed to create data plane for " << t->self_;
    return nullptr;
  }

  // Start serving control-plane RPCs, which fills in the bound control port.
  if ABSL_PREDICT_FALSE (!t->control_->Start()) {
    LOG(ERROR) << "failed to start control plane for " << t->self_;
    return nullptr;
  }
  DCHECK(t->self_.IsValid());
  return t;
}

absl::StatusOr<Handle> TransportImpl::Post(
    std::string_view peer, absl::Span<const Request> requests,
    absl::AnyInvocable<void(Status)> on_complete, const Metadata& metadata) {
  const absl::Time start_time = absl::Now();
  const uint64_t start_mono_us = TraceRecorder::NowUs();
  const TraceContext trace_ctx = engine_->Helper()->Tracer().MaybeStartTrace();
  const Endpoint endpoint = Endpoint::Create(peer);
  if ABSL_PREDICT_FALSE (!endpoint.HasNonzeroIpPort()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid peer endpoint ", peer));
  }

  if (requests.empty()) {
    return absl::InvalidArgumentError("Empty requests");
  }

  absl::flat_hash_set<Op> ops;
  for (const auto& request : requests) {
    if ABSL_PREDICT_FALSE (!request.IsValid()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid ", request.ToString()));
    }
    ops.insert(request.op);
  }

  // TODO(yongx): support mixed op types.
  if (ops.size() > 1) {
    return absl::InvalidArgumentError(
        "All requests must have the same op type");
  }

  return engine_->Enqueue(endpoint,
                          {requests, start_time, std::move(on_complete),
                           metadata, start_mono_us, trace_ctx});
}

TransportMetrics TransportImpl::GetTransportMetrics() const {
  static_assert(assumptions::kRulesToFollowWhenAddingNewMetrics);
  TransportMetrics m;
  control_->GetMetrics(m);
  engine_->GetMetrics(m);
  return m;
}

}  // namespace peregrine::internal
