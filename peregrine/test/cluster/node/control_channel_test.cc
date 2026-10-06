#include "peregrine/test/cluster/node/control_channel.h"

#include <sstream>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peregrine/test/cluster/node/config.h"
#include "peregrine/test/cluster/node/metrics.h"

namespace peregrine::cluster {
namespace {

using ::testing::HasSubstr;
using ::testing::StartsWith;

TEST(ControlChannelTest, EmitAndParseNodeReadyAndWaitForCommands) {
  std::stringstream in("noise\n  START \nSTOP\n");
  std::stringstream out;
  ControlChannel channel(in, out);

  std::vector<PeerTarget> targets = {
      PeerTarget{.endpoint = "127.0.0.1:10000", .raddr = 987654321ULL},
      PeerTarget{.endpoint = "127.0.0.1:10001", .raddr = 123456789ULL},
  };
  channel.EmitNodeReady(targets);

  EXPECT_THAT(
      out.str(),
      StartsWith(
          "PEREGRINE_NODE_READY "
          R"({"targets":["127.0.0.1:10000@987654321","127.0.0.1:10001@123456789"]})"));

  auto parsed_or = ParseNodeReadyLine(out.str());
  ASSERT_TRUE(parsed_or.ok()) << parsed_or.status();
  ASSERT_EQ(parsed_or->size(), 2);
  EXPECT_EQ((*parsed_or)[0].endpoint, "127.0.0.1:10000");
  EXPECT_EQ((*parsed_or)[0].raddr, 987654321ULL);
  EXPECT_EQ((*parsed_or)[1].endpoint, "127.0.0.1:10001");
  EXPECT_EQ((*parsed_or)[1].raddr, 123456789ULL);

  EXPECT_TRUE(channel.WaitForCommand(kStartCommand));
  EXPECT_TRUE(channel.WaitForCommand(kStopCommand));
  EXPECT_FALSE(channel.WaitForCommand("EXTRA"));
}

TEST(ControlChannelTest, EmitMetricsAndDoneLines) {
  std::stringstream in;
  std::stringstream out;
  ControlChannel channel(in, out);

  NodeMetricsSnapshot snap;
  snap.num_instances = 4;
  snap.xfer_size_bytes = 1;
  snap.transport.write.bytes = 4;
  snap.transport.rpc_requests_received = 4;

  channel.EmitMetricsSample(snap);
  channel.EmitWorkloadDone(snap);
  channel.EmitNodeDone(snap);

  const std::string output = out.str();
  EXPECT_THAT(output, HasSubstr("PEREGRINE_METRICS_SAMPLE {"));
  EXPECT_THAT(output, HasSubstr("PEREGRINE_WORKLOAD_DONE {"));
  EXPECT_THAT(output, HasSubstr("PEREGRINE_NODE_DONE {"));
  EXPECT_THAT(output, HasSubstr(R"("rpc_requests_received":4)"));
}

}  // namespace
}  // namespace peregrine::cluster
