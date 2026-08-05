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
class Display {
 public:
  // `integration` must outlive this object.
  explicit Display(const PeregrineIntegration& integration,
                   std::ostream& output = std::cout);

  void PrintHeader() const;
  void Print(absl::Time time) const;
  void PrintSummary() const;
  void PrintFooter() const;

  void InitNCurses();
  void ShutdownNCurses();


  // Required functions to make this class a `Runnable`.
  void Run() const { Print(absl::Now()); }
  absl::Duration cycle() const { return absl::Seconds(1); }

 private:
  std::string GenerateProgressString(absl::Time time) const;
  std::string GenerateStatsString() const;

  void NcursePrint(absl::Time now) const;
  void NormalPrint(absl::Time now) const;

  const PeregrineIntegration& integration_;
  std::ostream& output_;

  std::unique_ptr<NCurses> ncurses_;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_DISPLAY_H_
