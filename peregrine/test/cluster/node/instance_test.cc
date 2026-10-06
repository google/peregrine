#include "peregrine/test/cluster/node/instance.h"

#include <sys/socket.h>

#include "gtest/gtest.h"
#include "peregrine/src/util/util.h"
#include "peregrine/test/cluster/node/config.h"

namespace peregrine::cluster {
namespace {

TEST(InstanceTest, RaiseNoFileLimitSucceeds) {
  EXPECT_TRUE(RaiseNoFileLimit(65536).ok());
}

TEST(InstanceTest, CreateInstancesWithSharedBuffer) {
  NodeConfig cfg;
  cfg.ip = "127.0.0.1";
  cfg.num_instances = 4;
  cfg.base_control_port = util::FindFreePort(AF_INET, /*tcp=*/true);
  cfg.num_conns = 1;

  RegisteredBuffer shared_buf(/*size_bytes=*/64);
  auto instances = CreateInstances(cfg, &shared_buf, /*buf_size=*/64);
  ASSERT_TRUE(instances.ok()) << instances.status();
  ASSERT_EQ(instances->size(), 4);

  for (int i = 0; i < 4; ++i) {
    ASSERT_NE((*instances)[i], nullptr);
    EXPECT_EQ((*instances)[i]->BufferAddr(), shared_buf.AddrUint64());
    EXPECT_EQ((*instances)[i]->DataSize(), 64);
  }
  DestroyInstances(*instances);
}

TEST(InstanceTest, CreateInstancesWithPerInstanceBuffers) {
  NodeConfig cfg;
  cfg.ip = "127.0.0.1";
  cfg.num_instances = 3;
  cfg.base_control_port = util::FindFreePort(AF_INET, /*tcp=*/true);
  cfg.num_conns = 1;

  auto instances =
      CreateInstances(cfg, /*shared_buf=*/nullptr, /*buf_size=*/32);
  ASSERT_TRUE(instances.ok()) << instances.status();
  ASSERT_EQ(instances->size(), 3);

  EXPECT_NE((*instances)[0]->BufferAddr(), (*instances)[1]->BufferAddr());
  EXPECT_NE((*instances)[1]->BufferAddr(), (*instances)[2]->BufferAddr());
  DestroyInstances(*instances);
}

}  // namespace
}  // namespace peregrine::cluster
