// ---------------------------------------------------------
// $ bazelisk run //peregrine/test/cluster/node:node
// ---------------------------------------------------------

#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "peregrine/test/cluster/node/cluster_node.h"
#include "peregrine/test/cluster/node/control_channel.h"
#include "peregrine/test/cluster/node/flags.h"
#include "peregrine/test/cluster/node/instance.h"

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  if (auto s = peregrine::cluster::RaiseNoFileLimit(); !s.ok()) {
    LOG(WARNING) << "Failed to raise RLIMIT_NOFILE: " << s;
  }

  auto config_or = peregrine::cluster::ReadNodeConfig();
  QCHECK_OK(config_or.status());

  peregrine::cluster::ControlChannel channel;
  QCHECK_OK(peregrine::cluster::ClusterNode::Run(*config_or, channel));
  return 0;
}
