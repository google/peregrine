#ifndef PEREGRINE_TEST_INTEGRATION_SETTINGS_H_
#define PEREGRINE_TEST_INTEGRATION_SETTINGS_H_

#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace peregrine::integration {

struct Settings {
  // Names for the 4 display components.
  static constexpr absl::string_view kSenderControlpathName =
      "sender controlpath";
  static constexpr absl::string_view kReceiverControlpathName =
      "receiver controlpath";
  static constexpr absl::string_view kSenderDatapathName = "sender datapath";
  static constexpr absl::string_view kReceiverDatapathName =
      "receiver datapath";

  absl::Time test_begin;
};


}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_SETTINGS_H_
