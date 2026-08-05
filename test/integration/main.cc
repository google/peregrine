// ---------------------------------------------------------
// $ blaze run //third_party/peregrine/test/integration:main
// ---------------------------------------------------------

#include <csignal>
#include <memory>

#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "test/integration/display.h"
#include "test/integration/integration.h"
#include "test/integration/runner.h"

namespace {
using ::peregrine::integration::Display;
using ::peregrine::integration::PeregrineIntegration;
using ::peregrine::integration::Runner;

// Global Peregrine instance used to handle SIGINT.
PeregrineIntegration* g_peregrine_ptr = nullptr;

void RegisterSigIntHandler() {
  struct sigaction sa = {};
  sa.sa_handler = [](int signum) {
    if (signum == SIGINT && g_peregrine_ptr != nullptr) {
      g_peregrine_ptr->Stop();
    }
  };
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, nullptr);
}
}  // namespace

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  // Create a test instance.
  PeregrineIntegration peregrine;
  g_peregrine_ptr = &peregrine;

  // Register SIGINT handler.
  RegisterSigIntHandler();

  // Run the integration test.
  Display display(peregrine);
  display.PrintHeader();
  auto display_runner = std::make_unique<Runner<Display>>(&display);
  // -------------------
  peregrine.Run();
  // -------------------
  display_runner.reset();
  display.PrintSummary();
  display.PrintFooter();

  return 0;
}
