#ifndef PEREGRINE_TEST_INTEGRATION_ERROR_INJECT_MID_CHUNK_DISCONNECT_H_
#define PEREGRINE_TEST_INTEGRATION_ERROR_INJECT_MID_CHUNK_DISCONNECT_H_

#include <sys/socket.h>
#include <sys/types.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "peregrine/test/integration/error_inject/error_injector.h"

namespace peregrine::integration {

// Error injector that disconnects a TCP data connection in the middle of a
// Peregrine chunk transfer.
class MidChunkDisconnect final : public ErrorInjector {
 public:
  enum class Mode {
    kEof,  // Graceful TCP FIN via ::shutdown(fd, SHUT_RDWR)
    kRst,  // Abrupt TCP RST via SO_LINGER(0) + dup2(/dev/null, fd)
  };

  enum class Point {
    kMidHeader,    // Cut after sending half of the 64-byte ChunkHeader
    kAfterHeader,  // Cut after sending the full 64-byte ChunkHeader (0 payload)
    kMidPayload,   // Cut after sending full 64-byte ChunkHeader + half payload
  };

  MidChunkDisconnect(Mode mode, Point point, uint64_t on_chunk,
                     uint64_t every_n_chunks);

  // Creates a MidChunkDisconnect injector from CLI flags.
  static std::unique_ptr<MidChunkDisconnect> Create();

  std::string_view Name() const override { return "mid_chunk_disconnect"; }

  uint64_t InjectedCount() const override {
    return injected_count_.load(std::memory_order_relaxed);
  }

  ssize_t OnSendMsg(int fd, const struct msghdr* msg, int flags,
                    SendMsgFn real_sendmsg) override;

 private:
  static constexpr size_t kChunkHeaderSize = 64;

  bool shouldInject(uint64_t chunk_seq) const;
  void injectDisconnect(int fd) const;

 private:
  const Mode mode_;
  const Point point_;
  const uint64_t on_chunk_;
  const uint64_t every_n_chunks_;
  std::atomic<uint64_t> chunk_count_{0};
  std::atomic<uint64_t> injected_count_{0};
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_ERROR_INJECT_MID_CHUNK_DISCONNECT_H_
