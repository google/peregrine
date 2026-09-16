#include "src/internal/base/nicinfo.h"

#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "src/internal/base/endpoint.h"
#include "src/util/nic.h"

namespace peregrine::internal::testing {
namespace {

using util::NicType::kInvalid;
using util::NicType::kIP;
using util::NicType::kRDMA;

TEST(NicInfoTest, IsValid) {
  const Endpoint ipv4 = Endpoint::Create("127.0.0.1:12345");
  const Endpoint ipv6 = Endpoint::Create("[::1]:12345");
  const Endpoint roce = Endpoint::Create("[2202:a05:7901:1000::]:1");
  const Endpoint v4mapped = Endpoint::Create("[::ffff:10.0.0.1]:54321");
  ASSERT_TRUE(ipv4.HasNonzeroIpPort());
  ASSERT_TRUE(ipv6.HasNonzeroIpPort());
  ASSERT_TRUE(roce.HasNonzeroIpPort());
  ASSERT_TRUE(v4mapped.HasNonzeroIpPort());

  // Empty name, invalid type, or empty endpoints.
  EXPECT_FALSE((NicInfo("", kInvalid, {}).IsValid()));
  EXPECT_FALSE((NicInfo("", kIP, {ipv4}).IsValid()));
  EXPECT_FALSE((NicInfo("lo", kInvalid, {ipv4}).IsValid()));
  EXPECT_FALSE((NicInfo("lo", kIP, {}).IsValid()));

  // Valid kIP NICs.
  EXPECT_TRUE((NicInfo("lo", kIP, {ipv4, ipv6}).IsValid()));

  // kRDMA NICs require IPv6 endpoints (including IPv4/mapped IPv6).
  EXPECT_TRUE((NicInfo("irdma0", kRDMA, {roce, v4mapped}).IsValid()));
  EXPECT_FALSE((NicInfo("irdma0", kRDMA, {roce, ipv4}).IsValid()));
  EXPECT_FALSE((NicInfo("irdma0", kRDMA, {ipv4}).IsValid()));
}

TEST(NicInfoTest, ToString) {
  const auto lo4 = Endpoint::Create("127.0.0.1:35247");
  const auto lo6 = Endpoint::Create("[::1]:35247");
  const NicInfo lo("lo", kIP, {lo4, lo6});
  EXPECT_EQ(lo.ToString(), "lo/ip/127.0.0.1:35247,[::1]:35247");
  LOG(INFO) << "lo: " << lo;

  const auto eth4a = Endpoint::Create("183.10.20.11:43521");
  const auto eth4b = Endpoint::Create("183.10.30.59:51691");
  const NicInfo eth("eth0", kIP, {eth4a, eth4b});
  EXPECT_EQ(eth.ToString(), "eth0/ip/183.10.20.11:43521,183.10.30.59:51691");
  LOG(INFO) << "eth: " << eth;

  const auto rdma1 = Endpoint::Create("[2202:a05:7901:1000::]:1");
  const NicInfo rdma("irdma0", kRDMA, {rdma1});
  EXPECT_EQ(rdma.ToString(), "irdma0/rdma/[2202:a05:7901:1000::]:1");
  LOG(INFO) << "rdma: " << rdma;
}

TEST(NicInfoTest, CreateValid) {
  const NicInfo lo = NicInfo::Create("lo/ip/127.0.0.1:35247,[::1]:35247");
  EXPECT_TRUE(lo.IsValid());
  EXPECT_EQ(lo.name, "lo");
  EXPECT_EQ(lo.type, kIP);
  ASSERT_EQ(lo.endpoints.size(), 2);
  EXPECT_EQ(lo.endpoints[0], Endpoint::Create("127.0.0.1:35247"));
  EXPECT_EQ(lo.endpoints[1], Endpoint::Create("[::1]:35247"));
  EXPECT_EQ(lo.ToString(), "lo/ip/127.0.0.1:35247,[::1]:35247");

  const NicInfo eth = NicInfo::Create("eth0/ip/10.0.0.1:4351,10.0.0.2:519");
  EXPECT_TRUE(eth.IsValid());
  EXPECT_EQ(eth.name, "eth0");
  EXPECT_EQ(eth.type, kIP);
  ASSERT_EQ(eth.endpoints.size(), 2);
  EXPECT_EQ(eth.ToString(), "eth0/ip/10.0.0.1:4351,10.0.0.2:519");

  const NicInfo rdma = NicInfo::Create("irdma0/rdma/[2202::]:1");
  EXPECT_TRUE(rdma.IsValid());
  EXPECT_EQ(rdma.name, "irdma0");
  EXPECT_EQ(rdma.type, kRDMA);
  ASSERT_EQ(rdma.endpoints.size(), 1);
  EXPECT_EQ(rdma.endpoints[0], Endpoint::Create("[2202::]:1"));
  EXPECT_EQ(rdma.ToString(), "irdma0/rdma/[2202::]:1");
}

TEST(NicInfoTest, CreateInvalid) {
  EXPECT_FALSE(NicInfo::Create("").IsValid());
  EXPECT_FALSE(NicInfo::Create("lo").IsValid());
  EXPECT_FALSE(NicInfo::Create("lo/ip").IsValid());
  EXPECT_FALSE(NicInfo::Create("lo/ip/").IsValid());
  EXPECT_FALSE(NicInfo::Create("/ip/127.0.0.1:12345").IsValid());
  EXPECT_FALSE(NicInfo::Create("lo//127.0.0.1:12345").IsValid());
  EXPECT_FALSE(NicInfo::Create("eth0/unknown/127.0.0.1:12345").IsValid());
  EXPECT_FALSE(NicInfo::Create("eth0/ip/invalid_endpoint").IsValid());
  EXPECT_FALSE(NicInfo::Create("eth0/ip/127.0.0.1:0").IsValid());
  EXPECT_FALSE(NicInfo::Create("eth0/ip/0.0.0.0:12345").IsValid());
  EXPECT_FALSE(NicInfo::Create("irdma0/rdma/10.0.0.1:1").IsValid());
}

TEST(NicInfoTest, Equality) {
  const NicInfo a = NicInfo::Create("eth0/ip/10.0.0.1:12345");
  const NicInfo b = NicInfo::Create("eth0/ip/10.0.0.1:12345");
  const NicInfo c = NicInfo::Create("eth1/ip/10.0.0.1:12345");
  const NicInfo d = NicInfo::Create("eth0/ip/10.0.0.2:12345");
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
  EXPECT_NE(a, d);
}

}  // namespace
}  // namespace peregrine::internal::testing
