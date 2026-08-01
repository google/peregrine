#include "test/integration/display.h"

#include <ostream>

#include "absl/strings/str_format.h"
#include "test/integration/integration.h"

namespace peregrine::integration {

Display::Display(const PeregrineIntegration& integration, std::ostream& output)
    : integration_(integration), output_(output) {}

void Display::PrintHeader() const {
  output_ << "====================================================\n";
  output_ << "Peregrine Single Process Integration Test Running...\n";
  output_ << "====================================================\n";
}

void Display::Print() const {
  output_ << absl::StrFormat(
      "Transfers: %d | Bytes: %d\r",
      integration_.n_transfers(),
      integration_.n_bytes());
  output_.flush();
}

void Display::PrintSummary() const {
  output_ << "\n--- Summary ---\n";
  output_ << absl::StrFormat("Total Transfers: %d\n",
                             integration_.n_transfers());
  output_ << absl::StrFormat("Total Bytes    : %d\n",
                             integration_.n_bytes());
}

void Display::PrintFooter() const {
  output_ << "==================================================\n";
}

}  // namespace peregrine::integration
