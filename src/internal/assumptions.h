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

}  // namespace peregrine::assumptions

#endif  // PEREGRINE_SRC_INTERNAL_ASSUMPTIONS_H_
