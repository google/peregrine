#include "test/integration/display.h"

#include <sys/resource.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

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
#include "test/integration/metrics.h"
#include "test/integration/ncurses.h"
#include "test/integration/settings.h"

namespace peregrine::integration {

namespace {
int GetMemUsageMiB() {
  struct rusage usage;
  const int ret = getrusage(RUSAGE_SELF, &usage);
  return ret == 0 ? usage.ru_maxrss >> 10 : -1;
}
}  // namespace

Display::Display(const PeregrineIntegration& integration, std::ostream& output)
    : integration_(integration), output_(output) {
  if (integration_.flags().enable_ncurses) {
    if (absl::StatusOr<std::unique_ptr<NCurses>> ncurses = NCurses::Create();
        ncurses.ok()) {
      ncurses_ = std::move(ncurses).value();
    }
  }
}

Display::~Display() { ncurses_.reset(); }

void Display::PrintHeader() const {
  output_ << "=====================================\n";
  output_ << "Peregrine integration test running...\n";
  output_ << "=====================================\n";
}

void Display::PrintFooter() const {
  output_ << "=====================================\n";
  output_ << "Peregrine integration test completed.\n";
  output_ << "=====================================\n";
}

void Display::Print(absl::Time time) const {
  if (ncurses_ != nullptr) {
    ncursePrint(time);
  } else {
    normalPrint(time);
  }
}

void Display::PrintSummary() const {
  std::vector<std::string> results;
  results.push_back(Metrics::GetControlpathDebugString(
      Component::kSenderControlpath));
  results.push_back(Metrics::GetControlpathDebugString(
      Component::kReceiverControlpath));
  results.push_back(Metrics::GetDatapathDebugString(
      Component::kSenderDatapath));
  results.push_back(Metrics::GetDatapathDebugString(
      Component::kReceiverDatapath));
  results.push_back(absl::StrFormat(
      "Test started at %s and ran for %s.",
      absl::FormatTime(integration_.settings().test_begin),
      absl::FormatDuration(absl::Now() - integration_.settings().test_begin)));
  results.push_back(genStats());

  output_ << "=====================================\n";
  output_ << "Peregrine integration test summary:\n";
  output_ << "=====================================\n";
  output_ << absl::StrCat(
      "\n", absl::StrJoin(results, "\n-----------------------------------\n"),
      "\n");
}

std::string Display::genProgress(absl::Time time) const {
  const absl::Duration elapsed = time - integration_.settings().test_begin;
  CHECK_GE(integration_.flags().test_duration, absl::Seconds(1));
  const auto p = std::min<uint64_t>(
      100U, 100U * elapsed / integration_.flags().test_duration);
  return absl::StrFormat(
      "Testing in progress %s (%d%%) for %s run using %d MiB memory\n",
      absl::FormatDuration(absl::Trunc(elapsed, absl::Milliseconds(1))), p,
      absl::FormatDuration(integration_.flags().test_duration),
      GetMemUsageMiB());
}

std::string Display::genStats() const {
  const auto stats = integration_.GetStats();
  return absl::StrFormat("Transfers: %d, Bytes: %d MiB (%.2f Gbps)\n",
                         stats.transfers_completed,
                         stats.bytes_transferred >> 20, stats.throughput_gbps);
}

void Display::ncursePrint(absl::Time now) const {
  CHECK_NE(ncurses_, nullptr);
  ncurses_->Print(NCursesWindow::kSenderControlpath, 0,
                  Metrics::GetControlpathDebugString(
                      Component::kSenderControlpath));
  ncurses_->Print(NCursesWindow::kReceiverControlpath, 0,
                  Metrics::GetControlpathDebugString(
                      Component::kReceiverControlpath));
  ncurses_->Print(NCursesWindow::kSenderDatapath, 0,
                  Metrics::GetDatapathDebugString(
                      Component::kSenderDatapath));
  ncurses_->Print(NCursesWindow::kReceiverDatapath, 0,
                  Metrics::GetDatapathDebugString(
                      Component::kReceiverDatapath));
  ncurses_->Print(NCursesWindow::kStats, 0, genStats());
  ncurses_->Print(NCursesWindow::kProgress, 0, genProgress(now));
  ncurses_->Refresh();
}

void Display::normalPrint(absl::Time now) const {
  std::vector<std::string> results;
  results.push_back(genProgress(now));
  results.push_back(Metrics::GetControlpathDebugString(
      Component::kSenderControlpath));
  results.push_back(Metrics::GetControlpathDebugString(
      Component::kReceiverControlpath));
  results.push_back(Metrics::GetDatapathDebugString(
      Component::kSenderDatapath));
  results.push_back(Metrics::GetDatapathDebugString(
      Component::kReceiverDatapath));
  output_ << absl::StrCat(
      "\n", absl::StrJoin(results, "\n-----------------------------------\n"),
      "\n");
}

}  // namespace peregrine::integration
