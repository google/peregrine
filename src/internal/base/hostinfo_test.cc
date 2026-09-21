#include "src/internal/base/hostinfo.h"

#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/nicinfo.h"
#include "src/util/nic.h"

namespace peregrine::internal::testing {
namespace {

TEST(HostInfoTest, Validity) {
  const Endpoint cp = Endpoint::Create("10.0.0.1:10000");
  const NicInfo lo = NicInfo::Create("lo/ip/127.0.0.1:35247,[::1]:35247");
  const NicInfo eth = NicInfo::Create("eth0/ip/10.0.0.2:51691");
  const NicInfo rdma = NicInfo::Create("irdma0/rdma/[2202::]:1");

  // Default constructed is invalid.
  EXPECT_FALSE(HostInfo().IsValid());

  // Invalid control plane listener.
  const auto a = HostInfo{
      .control_plane_listener = Endpoint::Create("0.0.0.0:10000"),
      .data_plane_listeners = {lo},
  };
  EXPECT_FALSE(a.IsValid());

  const auto b = HostInfo{
      .control_plane_listener = Endpoint::Create("10.0.0.1:0"),
      .data_plane_listeners = {lo},
  };
  EXPECT_FALSE(b.IsValid());

  // Empty data plane listeners.
  const auto c = HostInfo{
      .control_plane_listener = cp,
      .data_plane_listeners = {},
  };
  EXPECT_FALSE(c.IsValid());

  // Invalid NIC in data plane listeners.
  const auto d = HostInfo{
      .control_plane_listener = cp,
      .data_plane_listeners =
          {
              NicInfo("", util::NicType::kIP,
                      {Endpoint::Create("10.0.0.1:12345")}),
          },
  };
  EXPECT_FALSE(d.IsValid());

  // Duplicate endpoint between control plane and data plane.
  const auto e = HostInfo{
      .control_plane_listener = cp,
      .data_plane_listeners = {NicInfo::Create("eth0/ip/10.0.0.1:10000")},
  };
  EXPECT_FALSE(e.IsValid());

  // Duplicate endpoint within the same NIC.
  const auto f = HostInfo{
      .control_plane_listener = cp,
      .data_plane_listeners = {NicInfo::Create(
          "eth0/ip/10.0.0.1:43521,10.0.0.1:43521")},
  };
  EXPECT_FALSE(f.IsValid());

  // Duplicate endpoint across different NICs.
  const auto g = HostInfo{
      .control_plane_listener = cp,
      .data_plane_listeners =
          {
              NicInfo::Create("eth0/ip/10.0.0.1:43521"),
              NicInfo::Create("eth1/ip/10.0.0.1:43521"),
          },
  };
  EXPECT_FALSE(g.IsValid());

  // Valid single NIC and multi/NIC configurations.
  const auto h = HostInfo{
      .control_plane_listener = cp,
      .data_plane_listeners = {lo},
  };
  EXPECT_TRUE(h.IsValid());

  const auto i = HostInfo{
      .control_plane_listener = cp,
      .data_plane_listeners = {lo, eth, rdma},
  };
  EXPECT_TRUE(i.IsValid());
}

TEST(HostInfoTest, CreateValid) {
  constexpr std::string_view kInput =
      "10.0.0.1:10000;\n"
      "  lo/ip/127.0.0.1:35247,[::1]:35247;\n"
      "  eth0/ip/10.0.0.1:43521,10.0.0.2:51691;\n"
      "  irdma0/rdma/[2202:a05:7901:1000::]:1";

  const HostInfo host = HostInfo::Create(kInput);
  ASSERT_TRUE(host.IsValid());
  EXPECT_EQ(host.control_plane_listener, Endpoint::Create("10.0.0.1:10000"));
  ASSERT_EQ(host.data_plane_listeners.size(), 3);
  EXPECT_EQ(host.data_plane_listeners[0],
            NicInfo::Create("lo/ip/127.0.0.1:35247,[::1]:35247"));
  EXPECT_EQ(host.data_plane_listeners[1],
            NicInfo::Create("eth0/ip/10.0.0.1:43521,10.0.0.2:51691"));
  EXPECT_EQ(host.data_plane_listeners[2],
            NicInfo::Create("irdma0/rdma/[2202:a05:7901:1000::]:1"));
  LOG(INFO) << host;
}

TEST(HostInfoTest, CreateInvalid) {
  EXPECT_FALSE(HostInfo::Create("").IsValid());
  EXPECT_FALSE(HostInfo::Create("10.0.0.1:10000").IsValid());
  EXPECT_FALSE(HostInfo::Create("invalid_cp; lo/ip/127.0.0.1:35247").IsValid());
  EXPECT_FALSE(HostInfo::Create("0.0.0.0:100; lo/ip/127.0.0.1:3547").IsValid());
  EXPECT_FALSE(HostInfo::Create("10.0.0.1:0; lo/ip/127.0.0.1:35247").IsValid());
  EXPECT_FALSE(HostInfo::Create("10.0.0.1:10000; invalid_nic").IsValid());

  const std::string_view unknown_nic =
      "10.0.0.1:10000; eth0/unknown/10.0.0.1:43521";
  EXPECT_FALSE(HostInfo::Create(unknown_nic).IsValid());

  const std::string_view rdma_invalid_ipv4 =
      "10.0.0.1:10000; irdma0/rdma/10.0.0.1:1";
  EXPECT_FALSE(HostInfo::Create(rdma_invalid_ipv4).IsValid());

  const std::string_view dup_endpoint_with_control =
      "10.0.0.1:10000; eth0/ip/10.0.0.1:10000";
  EXPECT_FALSE(HostInfo::Create(dup_endpoint_with_control).IsValid());

  const std::string_view dup_endpoint_across_nics =
      "10.0.0.1:10000; eth0/ip/10.0.0.1:43521; eth1/ip/10.0.0.1:43521";
  EXPECT_FALSE(HostInfo::Create(dup_endpoint_across_nics).IsValid());
}

TEST(HostInfoTest, ToString) {
  const std::string_view input =
      "10.0.0.1:10000; lo/ip/127.0.0.1:35247,[::1]:35247; "
      "eth0/ip/10.0.0.1:43521,10.0.0.2:51691; "
      "irdma0/rdma/[2202:a05:7901:1000::]:1";
  const HostInfo host = HostInfo::Create(input);
  ASSERT_TRUE(host.IsValid());
  LOG(INFO) << "host: " << host;

  EXPECT_EQ(host.ToString(), input);
}

TEST(HostInfoTest, Equality) {
  const HostInfo a = HostInfo::Create("10.0.0.1:10000; eth0/ip/10.0.0.1:43521");
  const HostInfo b = HostInfo::Create("10.0.0.1:10000; eth0/ip/10.0.0.1:43521");
  const HostInfo c = HostInfo::Create("10.0.0.1:10001; eth0/ip/10.0.0.1:43521");
  const HostInfo d = HostInfo::Create("10.0.0.1:10000; eth1/ip/10.0.0.1:43521");

  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
  EXPECT_NE(a, d);
}

}  // namespace
}  // namespace peregrine::internal::testing
