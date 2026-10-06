#include "peregrine/test/cluster/node/cluster_node.h"

#include <sys/socket.h>

#include <cstdint>
#include <istream>
#include <memory>
#include <ostream>
#include <streambuf>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "peregrine/src/util/thread.h"
#include "peregrine/src/util/util.h"
#include "peregrine/test/cluster/node/config.h"
#include "peregrine/test/cluster/node/control_channel.h"
#include "peregrine/test/workloads/serial_fixed_write.h"

namespace peregrine::cluster {
namespace {

using ::testing::HasSubstr;

// Simple thread-safe line pipe for driving ControlChannel in-process.
class InMemoryPipe : public std::streambuf {
 public:
  void WriteLine(std::string_view line) {
    absl::MutexLock lock(mu_);
    lines_.emplace_back(line);
    lines_.back().push_back('\n');
  }

  void Close() {
    absl::MutexLock lock(mu_);
    closed_ = true;
  }

 protected:
  int_type underflow() override {
    absl::MutexLock lock(mu_);
    auto has_data_or_closed = [this]() { return !lines_.empty() || closed_; };
    mu_.Await(absl::Condition(&has_data_or_closed));
    if (lines_.empty()) {
      return traits_type::eof();
    }
    current_ = lines_.front();
    lines_.erase(lines_.begin());
    setg(current_.data(), current_.data(), current_.data() + current_.size());
    return traits_type::to_int_type(*gptr());
  }

 private:
  absl::Mutex mu_;
  std::vector<std::string> lines_;
  std::string current_;
  bool closed_ = false;
};

class PipeOStream : public std::streambuf {
 public:
  explicit PipeOStream(InMemoryPipe* pipe) : pipe_(pipe) {}

 protected:
  int_type overflow(int_type ch) override {
    if (ch != traits_type::eof()) {
      char c = traits_type::to_char_type(ch);
      if (c == '\n') {
        pipe_->WriteLine(buf_);
        buf_.clear();
      } else {
        buf_.push_back(c);
      }
    }
    return ch;
  }

 private:
  InMemoryPipe* pipe_;
  std::string buf_;
};

void RunFanInLoopbackTest(bool share_buffer) {
  NodeConfig server_cfg;
  server_cfg.ip = "127.0.0.1";
  server_cfg.num_instances = 2;
  server_cfg.base_control_port = util::FindFreePort(AF_INET, /*tcp=*/true);
  server_cfg.num_conns = 1;
  server_cfg.workload_generator =
      std::make_shared<workloads::SerialFixedWrite>(1);
  server_cfg.share_buffer = share_buffer;
  server_cfg.metrics_interval = absl::ZeroDuration();

  InMemoryPipe server_in_buf;
  std::istream server_in(&server_in_buf);
  InMemoryPipe server_out_buf;
  std::istream server_out_reader(&server_out_buf);
  PipeOStream server_out_writer(&server_out_buf);
  std::ostream server_out(&server_out_writer);
  ControlChannel server_channel(server_in, server_out);

  util::Thread server_thread([&]() {
    EXPECT_TRUE(ClusterNode::Run(server_cfg, server_channel).ok());
    server_out_buf.Close();
  });

  std::string server_ready_line;
  ASSERT_TRUE(std::getline(server_out_reader, server_ready_line));
  auto server_targets_or = ParseNodeReadyLine(server_ready_line);
  ASSERT_TRUE(server_targets_or.ok()) << server_targets_or.status();
  ASSERT_EQ(server_targets_or->size(), 2);
  if (share_buffer) {
    EXPECT_EQ((*server_targets_or)[0].raddr, (*server_targets_or)[1].raddr);
  } else {
    EXPECT_NE((*server_targets_or)[0].raddr, (*server_targets_or)[1].raddr);
  }

  NodeConfig client_cfg;
  client_cfg.ip = "127.0.0.1";
  client_cfg.num_instances = 4;
  client_cfg.base_control_port =
      static_cast<uint16_t>(server_cfg.base_control_port + 50);
  client_cfg.num_conns = 1;
  client_cfg.workload_generator =
      std::make_shared<workloads::SerialFixedWrite>(1);
  client_cfg.targets = *server_targets_or;
  client_cfg.traffic_pattern = TrafficPattern::kAllToAll;
  client_cfg.num_xfers = 1;
  client_cfg.share_buffer = share_buffer;
  client_cfg.metrics_interval = absl::ZeroDuration();

  InMemoryPipe client_in_buf;
  std::istream client_in(&client_in_buf);
  InMemoryPipe client_out_buf;
  std::istream client_out_reader(&client_out_buf);
  PipeOStream client_out_writer(&client_out_buf);
  std::ostream client_out(&client_out_writer);
  ControlChannel client_channel(client_in, client_out);

  util::Thread client_thread([&]() {
    EXPECT_TRUE(ClusterNode::Run(client_cfg, client_channel).ok());
    client_out_buf.Close();
  });

  std::string client_ready_line;
  ASSERT_TRUE(std::getline(client_out_reader, client_ready_line));
  ASSERT_THAT(client_ready_line, HasSubstr(kNodeReadyPrefix));

  server_in_buf.WriteLine(kStartCommand);
  client_in_buf.WriteLine(kStartCommand);

  std::string workload_done_line;
  ASSERT_TRUE(std::getline(client_out_reader, workload_done_line));
  EXPECT_THAT(workload_done_line, HasSubstr(kWorkloadDonePrefix));
  EXPECT_THAT(workload_done_line, HasSubstr(R"("bytes":8,"errors":0)"));
  EXPECT_THAT(workload_done_line, HasSubstr(R"("count":8)"));

  client_in_buf.WriteLine(kStopCommand);
  server_in_buf.WriteLine(kStopCommand);

  std::string client_done_line;
  ASSERT_TRUE(std::getline(client_out_reader, client_done_line));
  EXPECT_THAT(client_done_line, HasSubstr("PEREGRINE_NODE_DONE "));

  std::string server_done_line;
  ASSERT_TRUE(std::getline(server_out_reader, server_done_line));
  EXPECT_THAT(server_done_line, HasSubstr("PEREGRINE_NODE_DONE "));
  EXPECT_THAT(server_done_line, HasSubstr(R"("rpc_requests_received":8)"));

  client_thread.join();
  server_thread.join();
}

TEST(ClusterNodeTest, MultiInstanceFanInOnLoopbackSharedBuffer) {
  RunFanInLoopbackTest(/*share_buffer=*/true);
}

TEST(ClusterNodeTest, MultiInstanceFanInOnLoopbackPerInstanceBuffers) {
  RunFanInLoopbackTest(/*share_buffer=*/false);
}

}  // namespace
}  // namespace peregrine::cluster
