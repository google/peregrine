#ifndef PEREGRINE_TEST_INTEGRATION_METRICS_H_
#define PEREGRINE_TEST_INTEGRATION_METRICS_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace peregrine::integration {

enum class Component {
  kSenderControlpath,
  kReceiverControlpath,
  kSenderDatapath,
  kReceiverDatapath,
};

// Thread-safe class to manage all metrics collected and displayed in the
// Peregrine integration test.
class Metrics final {
 public:
  Metrics() = delete;
  ~Metrics() = delete;

  // Sets the control path host metrics/status.
  static void SetControlpathInfo(Component c, std::string_view endpoint,
                                 std::string_view peer_endpoint,
                                 std::string_view mode,
                                 std::string_view status);

  // Sets the data path host metrics/status.
  static void SetDatapathInfo(Component c, std::string_view endpoint,
                              std::string_view peer_endpoint,
                              std::string_view status);

  // Increments transfers and bytes for a datapath host.
  static void IncrementTransfers(Component c, int64_t bytes);

  // Returns transfers completed count.
  static int64_t GetTransfers(Component c);

  // Returns bytes transferred count.
  static int64_t GetBytes(Component c);

  // Returns formatted debug string for controlpath host, or disabled info if
  // not set.
  static std::string GetControlpathDebugString(Component c);

  // Returns formatted debug string for datapath host, or disabled info if not
  // set.
  static std::string GetDatapathDebugString(Component c);

  // Resets the registered metrics.
  static void Reset();
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_METRICS_H_
