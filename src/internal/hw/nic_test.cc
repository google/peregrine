#include "src/internal/hw/nic.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"

namespace peregrine::internal::testing {
namespace {

TEST(NicsTest, EnumerateNics) {
  const absl::flat_hash_map<std::string, std::vector<std::string>> nics =
      EnumerateNics();
  ASSERT_FALSE(nics.empty());

  for (const auto& [nic, ips] : nics) {
    for (const auto& ip : ips) {
      LOG(INFO) << "nic " << nic << ", ip " << ip;
    }
  }
}

}  // namespace
}  // namespace peregrine::internal::testing
