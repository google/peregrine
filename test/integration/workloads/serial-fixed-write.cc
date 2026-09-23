#include "test/integration/workloads/serial-fixed-write.h"

#include <cstddef>
#include <string_view>

#include "absl/log/check.h"
#include "absl/time/time.h"
#include "src/api/transport_types.h"
#include "test/integration/workloads/batch-generator.h"

namespace peregrine::integration {

SerialFixedWrite::SerialFixedWrite(std::string_view peer, Byte* laddr,
                                   Byte* raddr, size_t len)
    : peer_(peer), laddr_(laddr), raddr_(raddr), len_(len) {
  CHECK(!peer_.empty());
  CHECK(laddr_ != nullptr);
  CHECK(raddr_ != nullptr);
  CHECK_GT(len_, 0);
}

PostBatch SerialFixedWrite::NextBatch() const {
  PostBatch batch;
  batch.items.push_back(PostItem{
      .peer = peer_,
      .requests =
          {
              Request{
                  .op = Op::kWrite,
                  .laddr = laddr_,
                  .raddr = raddr_,
                  .len = len_,
              },
          },
      .on_complete = nullptr,
  });
  batch.sleep = absl::ZeroDuration();
  return batch;
}

}  // namespace peregrine::integration
