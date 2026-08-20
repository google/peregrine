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
// The `absl::Hash` values are stable only within a single instance of process
// invocation. Across different process invocations of even the same program,
// `absl::Hash` of the same key yields different values.
inline constexpr bool kAbslHashIsStableOnlyInOneProcessInvocation = true;

// Assumptions about control/data plane.
// ---------------------------------------------------------------------------
//
// When a Peregrine transport instance starts, it needs to understand its host
// environment first. It enumerates all the network interface cards (NICs) on
// the host, and collects software information (such as ChunkHeader versions
// supported, see the kChunkHeaderHasBackwardForwardCompatibilityIssue below).
//
// Then Peregrine starts a control-plane gRPC (https://github.com/grpc/grpc)
// server bound to an arbitrary NIC and a predetermined port (e.g., `ip:port_c`,
// which can be used to identify the Peregrine instance itself). It also starts
// multiple data-plane TCP listeners, one per NIC (e.g. on `ip0::port_d0`,
// `ip1::port_d1`, etc.). The data-plane TCP listeners are used to create TCP
// connections bounded to specific NICs to carry data-plane traffic.
//
// Knowing the transport's control-plane listening address `ip:port_c`, other
// peer transports can connect to it to query its host environment information
// (such as data-plane TCP listener addresses, supported ChunkHeader versions)
// and exchange control messages. Alternatively, each transport can report its
// host info to a central server (such as https://github.com/etcd-io/etcd),
// which can then be queried by all the transports.
inline constexpr bool kTcpListenersOfControlAndDataPlanesAreSeparate = true;

// Continuing the above control/data plane discussion, here is how Peregrine
// starts:
//  - Peregrine enumerates all the network interface cards (NICs).
//  - User provides an `ip:port_c` endpoint (not a HostInfo object), where `ip`
//    is either zero or one of the NIC's IP address, and `port_c` is the same
//    well-known port for all Peregrine instances. If the provided `ip` is zero,
//    Peregrine will pick an arbitrary NIC to replace the `ip` field.
//  - Peregrine starts a control-plane gRPC server bound to `ip:port_c`.
//  - It starts multiple data-plane per-NIC TCP listeners on arbitrary ports.
//  - `TransportImpl` owns a HostInfo object. The control plane will fill
//    its `control_plane_listener` field, and the data plane will fill its
//    `data_plane_listeners` field. Note that all the endpoints in the HostInfo
//    object are valid, meaning they have non-zero IP and port.
//  - The `HostInfo.control_plane_listener` endpoint will be used to identify
//    the Peregrine instance itself.
inline constexpr bool kHostInfoDependsOnControlAndDataPlanes = true;

// Assumptions about control messages.
// ---------------------------------------------------------------------------
//
// There are multiple types of messages in the control plane, such as transport
// request, host info, software info, etc. All of them are wrapped in a single
// control message using protobuf's `oneof` feature. All the control message
// exchanges are done over gRPC. (The data plane, due to efficiency reasons,
// runs over TCP/RDMA/..., to minimize the middle layers as much as possible.)
inline constexpr bool kThereIsOnlyOneWrapperControlMessage = true;

// Assumptions about network MTU.
// ---------------------------------------------------------------------------
//
// We assume that network MTU is no more than 10 KiB. This should cover all the
// known cases including jumbo frames.
inline constexpr bool kNetworkMtuIsAtMostTenKiloBytes = true;

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
// the chunk header such as #chunks, chunk index/address/size, etc., and
// (2) the payload, which contains the actual chunk data. On the receive side,
// it has to read the chunk header first to get the chunk metadata, based on
// which it can then reads the payload and place it in its destination memory.
inline constexpr bool kBufferIsDividedIntoFixedSizeChunks = true;

// When sent over network, both the chunk header and payload are encrypted by
// layer-3 protocols (e.g., PSP https://github.com/google/psp). The encryption
// is transparent to the transport layer.
//
// Therefore, it is ok to store the destination memory address of chunk data
// in the chunk header. (We can further improve security by using a buffer id
// instead of the actual address, and let the receiver look up the destination
// address from the buffer id.)
inline constexpr bool kChunkHeaderAndPayloadAreEncryptedOnWire = true;

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

// Assumptions about the chunk write completion.
// ---------------------------------------------------------------------------
//
// The non zero-copy TCP send call returns immediately after the data is copied
// to the local kernel buffer, not after the data has been received by the
// remote peer. Therefore we use the ack chunk from the receiver to signal the
// chunk write completion. In comparison, zero-copy TCP send returns after the
// data has been received by the remote peer, subject to that the send buffer
// must be pinned in memory until the TCP acks all the sent packets.
//
// This assumption shows read and write are handled differently. For read, the
// receiver knows immediately when the data is received locally. For write, the
// sender needs to know after the data is received by the peer remotely.
//
// We will revisit this assumption periodically, and change the implementation
// accordingly.
inline constexpr bool kUseAckChunkToSignalChunkWriteCompletion = true;

// Assumptions about chunk header serialization.
// ---------------------------------------------------------------------------
//
// For performance reasons, chunk header is serialized to a fixed-size (64B)
// string using flatbuffer struct (https://github.com/google/flatbuffers).
// This saves one read call in the receiver. Otherwise, we have to first read
// a length field, and then read a variable-size string and parse it.
inline constexpr bool kChunkHeaderSerializesTo64BytesFixedSizeFlatBuf = true;

// Due to gradual rollout requirements, multiple versions of Peregrine binary
// can run in production. They must be able to talk to each other.
//
// In the control plane, all the control messages are serialized with protobuf,
// (https://github.com/protocolbuffers/protobuf), which addresses this
// compatibility issue.
//
// In general, a Peregrine binary might contain multiple versions of data-plane
// serialization schemas. When a local Peregrine starts, it uses gRPC to
// exchange host/software info with its peer. They will negotiate a common
// data-plane serialization schema that both sides can understand.
//
// In the data plane, here is what we do for the in-memory `ChunkHeader` struct
// and its corresponding `flatbuffer::v1::ChunkHeader` struct (assuming v1):
//  - We keep a single evolving version of `ChunkHeader`, so the code using it
//    never sees a new type.
//
//  - Multiple versions of `flatbuffer::ChunkHeader`s are kept in the same
//    binary. They are completely _independent_. We can remove a version only
//    if it's no longer used anymore. For this purpose, we monitor all the
//    `flatbuffer::ChunkHeader` versions running in production.
//
// Now we discuss how to address the Peregrine binary version compatibility:
//  - Order: Field position swap in the `ChunkHeader` is a non-breaking change.
//
//  - Addition: When a new field `NF` is added to `ChunkHeader`, a new version
//    `flatbuffer::v2::ChunkHeader` is created.
//    * Hardened behavior of old binaries: They don't know about `NF`.
//    * Expected behavior of new binary: It keeps `flatbuffer::v1::ChunkHeader`
//      and adds `flatbuffer::v2::ChunkHeader`. In its v2 (de)serializer, it
//      uses `NF` since it is the reason why `NF` is added. However, in its v1
//      serializer, it neglects `NF` since the old binaries don't know about it.
//      In its v1 deserializer, it generates a default value for `NF` since
//      `NF` is not present in the serialized data from old binaries.
//    * The new binary must be able to work with the default value of `NF` that
//      comes from its modified v1 deserializer.
//
//  - Deletion: To remove an existing field `OF` from `ChunkHeader`, again, a
//     new version `flatbuffer::v2::ChunkHeader` is created.
//    * Hardened behavior of old binaries: They know about `OF` and use it.
//    * Expected behavior of the new binary: In its v2 (de)serializer, it
//      neglects `OF` since it is the reason why `OF` is removed. In its v1
//      serializer, it serializes `OF` with a default value. In its v1
//      deserializer, it neglects `OF` since it doesn't want to use it any more.
//    * The old binaries must be able to work with the default value of `OF`.
//    * This essentially means every field in `ChunkHeader` must have a default
//      value, and all the binaries must work with these default values.
//
// In summary, `ChunkHeader` will have two kinds of fields:
//  - REQUIRED: not removeable, so no need to have a default value.
//  - OPTIONAL: removeable, thus must have a default value. We can wrap each
//      field `F` in `std::optional<F>` with `std::nullopt` as the default.
//
// When a new field is added, it may be hard to decide whether it's required or
// optional. This question has been discussed in protobuf world. The solution
// is, when in doubt, make the field OPTIONAL.
inline constexpr bool kChunkHeaderHasBackwardForwardCompatibilityIssue = true;
}  // namespace peregrine::assumptions

#endif  // PEREGRINE_SRC_INTERNAL_ASSUMPTIONS_H_
