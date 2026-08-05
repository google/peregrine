#ifndef PEREGRINE_TEST_INTEGRATION_DISPLAY_H_
#define PEREGRINE_TEST_INTEGRATION_DISPLAY_H_

#include <iostream>
#include <memory>
#include <ostream>
#include <string>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "test/integration/integration.h"
#include "test/integration/ncurses.h"

namespace peregrine::integration {

// Controls the display of the integration test.
class Display final {
 public:
  explicit Display(const PeregrineIntegration& integration,
                   std::ostream& output = std::cout);
  ~Display();

  void PrintHeader() const;
  void Print(absl::Time time) const;
  void PrintSummary() const;
  void PrintFooter() const;

  // Required functions to make this class a `Runnable`.
  void Run() const { Print(absl::Now()); }
  absl::Duration Cycle() const { return absl::Seconds(1); }

  // Resets ncurses to restore the terminal screen.
  void Clear() { ncurses_.reset(); }

 private:
  std::string genProgress(absl::Time time) const;
  std::string genStats() const;

  void ncursePrint(absl::Time now) const;
  void normalPrint(absl::Time now) const;

 private:
  const PeregrineIntegration& integration_;
  std::unique_ptr<NCurses> ncurses_;
  std::ostream& output_;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_DISPLAY_H_
