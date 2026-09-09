#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_PSP_PSP_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_PSP_PSP_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string>
#include <string_view>

#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "src/api/strong_int.h"

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
  std::string ToString() const {
    const std::string_view kv{(const char*)key.data(), key.size()};
    return absl::StrFormat("PspToken(spi=%u, gen=%u, key=0x%s)", spi.value(),
                           gen.value(), absl::BytesToHexString(kv));
  }
};
static_assert(sizeof(PspToken) == 24);

inline std::ostream& operator<<(std::ostream& os, const PspToken& token) {
  return os << token.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_PSP_PSP_H_
