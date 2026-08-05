#include "test/integration/display.h"

#include <sys/resource.h>

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "test/integration/flags.h"
#include "test/integration/integration.h"
#include "test/integration/ncurses.h"
#include "test/integration/settings.h"

namespace peregrine::integration {

namespace {

template <typename T>
concept HasDebugString = requires(const T& t) {
  { t.DebugString() } -> std::same_as<std::string>;
};

template <HasDebugString Component, typename... Args>
std::string DebugStringIfEnabled(absl::string_view name,
                                 const Component* component, Args&&... args) {
  if (component == nullptr) {
    return absl::StrFormat("%s: disabled", name);
  }
  return component->DebugString(std::forward<Args>(args)...);
}

uint64_t GetMemUsageKB() {
  struct rusage usage;
  const int ret = getrusage(RUSAGE_SELF, &usage);
  DCHECK_EQ(ret, 0);
  return ret == 0 ? usage.ru_maxrss : 0;
}

}  // namespace

Display::Display(const PeregrineIntegration& integration, std::ostream& output)
    : integration_(integration), output_(output) {}

void Display::InitNCurses() {
  if (absl::GetFlag(FLAGS_enable_ncurses)) {
    absl::StatusOr<std::unique_ptr<NCurses>> ncurses = NCurses::Create();
    if (ncurses.ok()) {
      ncurses_ = std::move(*ncurses);
    }
  }
}

void Display::ShutdownNCurses() { ncurses_.reset(); }


void Display::PrintHeader() const {
  output_ << "=====================================\n";
  output_ << "Peregrine integration test running...\n";
  output_ << "=====================================\n";
}

void Display::Print(absl::Time time) const {
  if (ncurses_ != nullptr) {
    NcursePrint(time);
  } else {
    NormalPrint(time);
  }
}

void Display::PrintSummary() const {
  std::vector<std::string> results;
  results.push_back(DebugStringIfEnabled(Settings::kSenderControlpathName,
                                         integration_.controlpath_sndr()));
  results.push_back(DebugStringIfEnabled(Settings::kReceiverControlpathName,
                                         integration_.controlpath_rcvr()));
  results.push_back(DebugStringIfEnabled(Settings::kSenderDatapathName,
                                         integration_.datapath_sndr()));
  results.push_back(DebugStringIfEnabled(Settings::kReceiverDatapathName,
                                         integration_.datapath_rcvr()));
  results.push_back(absl::StrFormat(
      "Test started at %s and ran for %s.",
      absl::FormatTime(integration_.settings().test_begin),
      absl::FormatDuration(absl::Now() - integration_.settings().test_begin)));
  results.push_back(GenerateStatsString());

  output_ << "=====================================\n";
  output_ << "Peregrine integration test summary:\n";
  output_ << "=====================================\n";
  output_ << absl::StrCat(
      "\n", absl::StrJoin(results, "\n-----------------------------------\n"),
      "\n");
}

void Display::PrintFooter() const {
  output_ << "=====================================\n";
  output_ << "Peregrine integration test completed.\n";
  output_ << "=====================================\n";
}

std::string Display::GenerateProgressString(absl::Time time) const {
  const absl::Duration elapsed = time - integration_.settings().test_begin;
  CHECK_GE(integration_.settings().test_duration, absl::Seconds(1));
  const auto p = std::min<uint64_t>(
      100U, 100U * elapsed / integration_.settings().test_duration);
  return absl::StrFormat(
      "Testing in progress %s (%d%%) for %s run (%d KB max mem used).\n",
      absl::FormatDuration(elapsed), p,
      absl::FormatDuration(integration_.settings().test_duration),
      GetMemUsageKB());
}

std::string Display::GenerateStatsString() const {
  const auto stats = integration_.GetStats();
  return absl::StrFormat("Transfers: %d, Bytes: %d (%.2f MB/s)\n",
                         stats.transfers_completed, stats.bytes_transferred,
                         stats.throughput_mbps);
}

void Display::NcursePrint(absl::Time now) const {
  CHECK_NE(ncurses_, nullptr);
  ncurses_->Print(NCursesWindow::kSenderControlpath, 0,
                  DebugStringIfEnabled(Settings::kSenderControlpathName,
                                       integration_.controlpath_sndr()));
  ncurses_->Print(NCursesWindow::kReceiverControlpath, 0,
                  DebugStringIfEnabled(Settings::kReceiverControlpathName,
                                       integration_.controlpath_rcvr()));
  ncurses_->Print(NCursesWindow::kSenderDatapath, 0,
                  DebugStringIfEnabled(Settings::kSenderDatapathName,
                                       integration_.datapath_sndr()));
  ncurses_->Print(NCursesWindow::kReceiverDatapath, 0,
                  DebugStringIfEnabled(Settings::kReceiverDatapathName,
                                       integration_.datapath_rcvr()));
  ncurses_->Print(NCursesWindow::kStats, 0, GenerateStatsString());
  ncurses_->Print(NCursesWindow::kProgress, 0, GenerateProgressString(now));
  ncurses_->Refresh();
}

void Display::NormalPrint(absl::Time now) const {
  std::vector<std::string> results;
  results.push_back(GenerateProgressString(now));
  results.push_back(DebugStringIfEnabled(Settings::kSenderControlpathName,
                                         integration_.controlpath_sndr()));
  results.push_back(DebugStringIfEnabled(Settings::kReceiverControlpathName,
                                         integration_.controlpath_rcvr()));
  results.push_back(DebugStringIfEnabled(Settings::kSenderDatapathName,
                                         integration_.datapath_sndr()));
  results.push_back(DebugStringIfEnabled(Settings::kReceiverDatapathName,
                                         integration_.datapath_rcvr()));
  output_ << absl::StrCat(
      "\n", absl::StrJoin(results, "\n-----------------------------------\n"),
      "\n");
}

}  // namespace peregrine::integration
