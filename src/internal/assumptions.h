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
// the chunk metadata such as #chunks, chunk index/address/size, etc., and
// (2) the payload, which contains the actual chunk data. On the receive side,
// it has to read the chunk header first to get the chunk metadata. It then
// reads the payload and place it in its destination memory.
inline constexpr bool kBufferIsDividedIntoFixedSizeChunks = true;

// When sent over network, both the chunk metadata and payload are encrypted by
// layer-3 protocols (e.g., PSP (https://github.com/google/psp). The encryption
// is transparent to the transport layer.
//
// Therefore, it is ok to store the destination memory address of chunk data
// in the chunk header. (We can further improve security by using a buffer id
// instead of the actual address, and let the receiver look up the destination
// address from the buffer id.)
inline constexpr bool kChunkMetadataAndPayloadAreEncryptedOnWire = true;

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
// a set of communication channels. As a result, multiple chunks often arrive at
// the receiver simultaneously. We assume that, in any time window, there are
// not many (ideally, no) duplicate chunk arrivals. For each chunk, there can
// be at most a few writer threads asking for permission to write the same chunk
// data. Therefore, the write contention of the same chunk at the receiver side
// is extremely low. On the other hand, multiple writer threads can write
// different chunks at the same time.
inline constexpr bool kReceiverSideChunkWriteContentionIsVeryLow = true;

// Assumptions about control/data plane.
// ---------------------------------------------------------------------------
//
// When a transport instance starts, it needs to understand its host environment
// first. For example, it enumerates all the network interface cards (NICs) on
// the host. Then the transport starts one control-plane TCP listener bound to
// an arbitrary NIC (e.g. on ip:port_c, which can be used to identify the
// transport instance itself). It also starts multiple data-plane TCP listeners
// one per NIC (e.g. on ip0::port_d0 and ip1::port_d1). The data-plane TCP
// listeners are used to create TCP connections bounded to specific NICs to
// carry data-plane traffic, not control-plane messages.
//
// Knowing the transport's control-plane listening address ip:port_c, other peer
// transports can connect to it to query its host environment information (such
// as data-plane TCP listener addresses) and exchange control messages.
// Alternatively, each transport can report its host info to a central server
// (such as `etcd`), which can then be queried by all the transports.
inline constexpr bool kTcpListenersOfControlAndDataPlanesAreSeparate = true;

// Assumptions about control messages.
// ---------------------------------------------------------------------------
//
// There are multiple types of messages in the control plane, such as transport
// request, host device info, etc. All of them are wrapped in a single control
// message using protobuf's `oneof` feature. This control message is serialized
// in two fields: a 4-byte network-byte-order length field and a variable-size
// string (serialization of the inner message, at most 1024 bytes). In total,
// a serialized control message is at most 1028 bytes, well below the widely
// used network MTU of 1500 bytes.
inline constexpr bool kThereIsOnlyOneWrapperControlMessageAtMost1KiB = true;

}  // namespace peregrine::assumptions

#endif  // PEREGRINE_SRC_INTERNAL_ASSUMPTIONS_H_
