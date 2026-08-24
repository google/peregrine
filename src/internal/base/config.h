#ifndef PEREGRINE_SRC_INTERNAL_BASE_CONFIG_H_
#define PEREGRINE_SRC_INTERNAL_BASE_CONFIG_H_

namespace peregrine::internal {

// This struct holds the configuration for the transport.
// It is thread-safe since it's read-only after construction.
struct Config {
  // The number of connections to maintain per peer.
  int num_conns_per_peer = 1;

  // Returns true iff the config is valid.
  bool IsValid() const {
    return 1 <= num_conns_per_peer && num_conns_per_peer <= 100;
  }
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BASE_CONFIG_H_
