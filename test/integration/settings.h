#ifndef PEREGRINE_TEST_INTEGRATION_SETTINGS_H_
#define PEREGRINE_TEST_INTEGRATION_SETTINGS_H_

#include <string_view>

#include "absl/time/time.h"

namespace peregrine::integration {

struct Settings {
  // Names for the 4 display components.
  static constexpr std::string_view kSenderControlpathName =
      "sender controlpath";
  static constexpr std::string_view kReceiverControlpathName =
      "receiver controlpath";
  static constexpr std::string_view kSenderDatapathName = "sender datapath";
  static constexpr std::string_view kReceiverDatapathName = "receiver datapath";

  absl::Time test_begin;
};


}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_SETTINGS_H_
