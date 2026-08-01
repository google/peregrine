#ifndef PEREGRINE_TEST_INTEGRATION_DISPLAY_H_
#define PEREGRINE_TEST_INTEGRATION_DISPLAY_H_

#include <iostream>
#include <ostream>

#include "absl/time/time.h"
#include "test/integration/integration.h"

namespace peregrine::integration {

// Controls the display of the integration test.
class Display {
 public:
  // `integration` must outlive this object.
  explicit Display(const PeregrineIntegration& integration,
                   std::ostream& output = std::cout);

  void PrintHeader() const;
  void Print() const;
  void PrintSummary() const;
  void PrintFooter() const;

  // Required functions to make this class a `Runnable`.
  void Run() const { Print(); }
  absl::Duration cycle() const { return absl::Seconds(1); }

 private:
  const PeregrineIntegration& integration_;
  std::ostream& output_;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_DISPLAY_H_
