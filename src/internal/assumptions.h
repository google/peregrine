#ifndef PEREGRINE_SRC_INTERNAL_ASSUMPTIONS_H_
#define PEREGRINE_SRC_INTERNAL_ASSUMPTIONS_H_

namespace peregrine::assumptions {

// Assumptions about the threading model.
// ---------------------------------------------------------------------------
//
// The transport API defined by `src/api/transport.h` is asynchronous. User
// applications simply post communication requests to the transport, and later
// poll for (or get notified of) their status. Our transport implementation has
// its own worker threads to process the requests and update their status.
inline constexpr bool kTransportImplementationHasItsOwnThreads = true;

// Assumptions about hashing.
// ---------------------------------------------------------------------------
//
// The absl::Hash values are stable only within a single instance of process
// invocation. Across different process invocations of even the same program,
// absl::Hash of the same key yields different values.
inline constexpr bool kAbslHashIsStableOnlyInOneProcessInvocation = true;

// Assumptions about transport request, handle, buffer and chunks.
// ---------------------------------------------------------------------------
//
// A transport request, identified by a handle, includes one or more buffers.
// Each buffer is a contiguous memory space. It is divided into a sequence of
// chunks sent over multiple parallel transport channels (e.g., TCP connections
// or RDMA queue pairs).
//
// Chunk is the smallest data transmit unit. Chunk size is fixed and determined
// before transmission happens. All the chunks have the same size, except for
// the last one, which may be smaller.
//
// Over the wire, each chunk has two parts: (1) the header, which contains
// chunk metadata (buffer id, chunk size, chunk index, etc.), and (2) the
// payload, which contains the actual chunk data. On the receive side, it has
// to read the chunk header first to get the chunk metadata, esp. the chunk
// index (to figure out the destination memory address) and the chunk size.
// It then reads the payload and place it in its destination memory. (The tail
// chunk maybe shorter than the fixed chunk size. It is handled specially.)
inline constexpr bool kBufferIsDividedIntoFixedSizeChunks = true;

// Assumptions about network MTU.
// ---------------------------------------------------------------------------
//
// We assume that network MTU is no more than 10 KiB. This should cover all the
// known cases including jumbo frames.
inline constexpr bool kNetworkMtuIsAtMostTenKiloBytes = true;

// Assumptions about chunk metadata serialization.
// ---------------------------------------------------------------------------
//
// For performance reasons, chunk metadata is serialized to a FIXED-SIZE string
// using flatbuffer struct (https://github.com/google/flatbuffers). This saves
// one read call in the receiver. Otherwise, we have to first read a length
// field, and then read a variable-size string and parse it. In other words,
// we treat chunk metadata as a fixed-size header.
inline constexpr bool kChunkMetadataSerializesToFixedSizeFlatBufString = true;

// Assumptions about the chunk receive contention.
// ---------------------------------------------------------------------------
//
// A transport request contains one or more non-overlapping buffers, each of
// which is further split into many fixed-size, non-overlapping chunks sent over
// a set of communication channels. As a result, multiple chunks may arrive at
// the receiver simultaneously. We assume that, in any time window, there are
// not many (ideally, no) duplicate chunk arrivals. For each chunk, there can
// be at most a few (ideally, one) writers asking for permission to write its
// data. That is, the write contention of the same chunk at the receiver side is
// low. However, multiple writers can write different chunks at the same time.
inline constexpr bool kLowChunkWritingContentionAtReceiverSide = true;

}  // namespace peregrine::assumptions

#endif  // PEREGRINE_SRC_INTERNAL_ASSUMPTIONS_H_
