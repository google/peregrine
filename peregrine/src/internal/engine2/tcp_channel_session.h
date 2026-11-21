#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_TCP_CHANNEL_SESSION_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_TCP_CHANNEL_SESSION_H_

#include <array>
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/chunk/chunk_tracker.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/chunk/header_flatbuf.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal::engine2 {

// Drives non-blocking TCP wire framing (writes, reads, and ACKs) on one
// `Channel`.
//
// Thread-safety: Thread-compatible; owned and driven by a single worker thread.
class TcpChannelSession final {
 public:
  // Creates a session over `channel` between `self_peer` and `remote_peer`.
  // `work_queue`, `outgoing`, and `incoming` must outlive the session.
  TcpChannelSession(std::unique_ptr<Channel> channel, PeerId self_peer,
                    PeerId remote_peer, PeerWorkQueue& work_queue,
                    RequestTracker& outgoing, RequestTracker& incoming,
                    int worker_id);

  DISALLOW_COPY(TcpChannelSession);
  DISALLOW_MOVE(TcpChannelSession);

  // Fails any in-flight outgoing post and releases any acquired incoming chunk.
  ~TcpChannelSession();

  // Returns true iff sending is currently paused due to backpressure.
  bool IsTxBlocked() const { return tx_.blocked; }

  // Marks the session writable again after backpressure clears.
  void ClearTxBlocked() { tx_.blocked = false; }

  // Returns true iff control frames (ACK or failure) are waiting to be sent.
  bool HasPendingControl() const { return !tx_.control_queue.empty(); }

  // Sends pending control frames and outgoing chunks until backpressure or the
  // per-call write limit is reached. Sets `has_more` if more work remains;
  // returns false on fatal write error.
  bool DriveTx(bool& has_more);

  // Reads and processes all currently available incoming frames. Pass
  // `peer_closed = true` if the peer has closed its write side. Returns false
  // on fatal error, protocol error, or EOF.
  bool DriveRx(bool peer_closed = false);

 private:
  // Outgoing send queue and active frame state (never moved while `cursor` is
  // live).
  struct TxState {
    bool blocked = false;
    // Serialized 64B ACK/failure frames, sent ahead of data chunks.
    std::deque<std::string> control_queue;
    // Most recent post handle whose `PostInfo` was sent on this channel.
    Handle last_post_info_handle = kInvalidHandle;

    // Keeps caller's payload buffer alive while `cursor` points into it.
    std::shared_ptr<const QueuedPost> post;
    // Leading `PostInfoHeader` + `RequestInfo` bytes when starting a new post.
    std::string post_info;
    std::string header;
    std::optional<IoVecCursor> cursor;

    void ResetFrame() {
      cursor.reset();
      post.reset();
      post_info.clear();
      header.clear();
    }
  };

  // Incoming stream state: reading a 64B header or one of three payload phases.
  struct RxState {
    struct ReadingHeader {
      std::array<Byte, HeaderUtil::kSize> buf = {};
    };
    struct ReadingPostInfo {
      PostInfoHeader header = {};
      std::string payload;
    };
    struct ReadingData {
      DataHeader header = {};
      Byte* dst = nullptr;
      // Acquired chunk slot; must be released on completion or abort.
      ChunkTracker* tracker = nullptr;
    };
    // Discards bytes of unknown, duplicate, or invalid chunks safely.
    struct Draining {
      DataHeader header = {};
      size_t remaining = 0;
      // True when draining an already-completed duplicate chunk to re-ACK it.
      bool ack_after = false;
    };

    std::variant<ReadingHeader, ReadingPostInfo, ReadingData, Draining> phase;
    std::optional<IoVecCursor> cursor;
    // Lazily allocated scratch buffer used only in `Draining`.
    std::vector<Byte> drain_buf;
  };

  // Stages the next outgoing frame to send. Returns false if idle.
  bool prepareNextTx();

  // Stages a `Data` chunk frame for `claim`, optionally prepending `PostInfo`.
  void prepareChunkTx(const ChunkClaim& claim, bool include_post_info);

  // Sets up the receive cursor for the active receive phase.
  void prepareRxCursor();

  // Dispatches a completed wire header. Returns false on protocol error.
  bool onHeader(const RxState::ReadingHeader& hdr);

  // Processes an incoming `DataHeader` (ACK or data chunk).
  bool onDataHeader(const DataHeader& chunk);

  // Processes an incoming `PostInfoHeader`.
  bool onPostInfoHeader(const PostInfoHeader& info);

  // Completes the current incoming payload and advances receive state.
  void onPayload();

  // Fails any in-flight outgoing post and releases any acquired incoming chunk.
  void abortInFlight();

 private:
  const int worker_id_;
  const PeerId self_peer_;
  const PeerId remote_peer_;

  // Non-owning, thread-safe references; all must outlive this session.
  PeerWorkQueue& work_queue_;
  RequestTracker& outgoing_;
  RequestTracker& incoming_;

  // Owned, single-threaded per-channel state.
  std::unique_ptr<Channel> channel_;
  TxState tx_;
  RxState rx_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_TCP_CHANNEL_SESSION_H_
