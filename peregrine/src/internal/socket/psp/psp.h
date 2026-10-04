#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_PSP_PSP_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_PSP_PSP_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string>

#include "absl/strings/str_format.h"
#include "peregrine/src/util/strong_int.h"

namespace peregrine::internal {

constexpr size_t kPspKeyLen = 16;

DEFINE_STRONG_INT_TYPE(Spi, uint32_t);
DEFINE_STRONG_INT_TYPE(Gen, uint32_t);

// This struct defines a psp token.
struct PspToken final {
  Spi spi;  // secure parameter index
  Gen gen;  // generation
  std::array<uint8_t, kPspKeyLen> key;

  // Constructor.
  explicit PspToken(Spi spi = Spi(0), Gen gen = Gen(0),
                    std::array<uint8_t, kPspKeyLen> key = {})
      : spi(spi), gen(gen), key(key) {}

  // Returns true iff the psp token is valid.
  bool IsValid() const { return spi.value() != 0; }

  // Returns a string representation of the psp token.
  // The key is deliberately redacted, since this is used for logging.
  std::string ToString() const {
    return absl::StrFormat("PspToken(spi=%u, gen=%u, key=<redacted>)",
                           spi.value(), gen.value());
  }
};
static_assert(sizeof(PspToken) == 24);

inline std::ostream& operator<<(std::ostream& os, const PspToken& token) {
  return os << token.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_PSP_PSP_H_
