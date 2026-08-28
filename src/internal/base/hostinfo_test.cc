#include "src/internal/base/hostinfo.h"

#include <string>

#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "src/internal/base/endpoint.h"

namespace peregrine::internal::testing {
namespace {

TEST(HostInfoTest, Validity) {
  const HostInfo host1 = HostInfo::Create("127.0.0.1:12345");
  EXPECT_FALSE(host1.IsValid());
  LOG(INFO) << host1;

  const HostInfo host2 = HostInfo::Create("127.0.0.1:12345, [::1]:54321");
  EXPECT_TRUE(host2.IsValid());
  LOG(INFO) << host2;

  const Endpoint c = Endpoint::Create("10.0.0.1:10000");
  const Endpoint d0 = Endpoint::Create("10.0.0.1:35247");
  const Endpoint d1 = Endpoint::Create("10.0.0.2:51691");
  const HostInfo host3 = {.control_plane_listener = c,
                          .data_plane_listeners = {d0, d1}};
  EXPECT_TRUE(host3.IsValid());
  LOG(INFO) << host3;
}

TEST(HostInfoTest, ToString) {
  const Endpoint c = Endpoint::Create("127.0.0.1:12345");
  const Endpoint d1 = Endpoint::Create("127.0.0.1:54321");
  const Endpoint d2 = Endpoint::Create("[::1]:54321");
  const HostInfo host = {.control_plane_listener = c,
                         .data_plane_listeners = {d1, d2}};
  EXPECT_TRUE(host.IsValid());
  LOG(INFO) << host;
}

TEST(HostInfoTest, RdmaInterface) {
  const Endpoint c = Endpoint::Create("127.0.0.1:12345");
  const Endpoint d = Endpoint::Create("127.0.0.1:54321");
  const std::string gid(16, '\x01');
  const RdmaInterface rdma0 = {.name = "irdma0", .gid = gid, .port_num = 1};
  const RdmaInterface rdma1 = {.name = "irdma1", .gid = gid, .port_num = 1};
  const RdmaInterface rdma2 = {.name = "irdma0", .gid = "none", .port_num = 1};
  const HostInfo host = {
      .control_plane_listener = c,
      .data_plane_listeners = {d},
      .rdma_interfaces = {rdma0, rdma1},
  };
  EXPECT_TRUE(host.IsValid());
  LOG(INFO) << host;

  const HostInfo rdma_only = {
      .control_plane_listener = c,
      .data_plane_listeners = {},
      .rdma_interfaces = {rdma0, rdma1},
  };
  EXPECT_TRUE(rdma_only.IsValid());
  LOG(INFO) << rdma_only;

  const HostInfo duplicate_rdma = {
      .control_plane_listener = c,
      .data_plane_listeners = {},
      .rdma_interfaces = {rdma0, rdma0},
  };
  EXPECT_FALSE(duplicate_rdma.IsValid());

  const HostInfo invalid_gid = {
      .control_plane_listener = c,
      .data_plane_listeners = {},
      .rdma_interfaces = {rdma2},
  };
  EXPECT_FALSE(invalid_gid.IsValid());
}

}  // namespace
}  // namespace peregrine::internal::testing
