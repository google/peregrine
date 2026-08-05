#include "test/integration/ncurses.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include <ncurses.h>

namespace peregrine::integration {

absl::StatusOr<std::unique_ptr<NCurses>> NCurses::Create() {
  auto ncurses = std::unique_ptr<NCurses>(new NCurses());
  if (absl::Status s = ncurses->init(); !s.ok()) return s;
  return ncurses;
}

NCurses::~NCurses() { reset(); }

absl::Status NCurses::init() {
  stdscr_ = initscr();
  if (stdscr_ == nullptr) {
    return absl::InternalError("Failed to initialize ncurses standard screen.");
  }
  if (!isScreenBigEnough()) {
    const int lines = getmaxy(stdscr_);
    const int cols = getmaxx(stdscr_);
    reset();
    return absl::FailedPreconditionError(absl::StrFormat(
        "Screen size (%d, %d) is too small, need at least (%d, %d).", lines,
        cols, kScreenMinNumLines, kScreenMinNumCols));
  }
  cbreak();
  noecho();
  curs_set(0);

  if (!initWindows()) {
    reset();
    return absl::InternalError("Failed to initialize ncurses windows.");
  }
  return absl::OkStatus();
}

void NCurses::reset() {
  if (stdscr_ != nullptr) {
    resetWindows();
    curs_set(1);
    echo();
    nocbreak();
    endwin();
    stdscr_ = nullptr;
  }
}

bool NCurses::initWindows() {
  const int all_lines = getmaxy(stdscr_);
  const int all_cols = getmaxx(stdscr_);
  DCHECK_GE(all_lines, kScreenMinNumLines);
  DCHECK_GE(all_cols, kScreenMinNumCols);

  constexpr int tlines = 2, hlines = 3, qlines = 2;
  constexpr int vcols = 1, spcols = 1;
  const int tcols = all_cols, hcols = all_cols;
  const int scols = all_cols / 2, pcols = all_cols - scols;

  const int celines = all_lines - tlines - hlines - qlines;
  const int clines = celines / 2, dlines = celines - clines;

  const int ccols = all_cols - vcols - spcols * 4;
  const int scols_side = ccols / 2, rcols_side = ccols - scols_side;

  int x = 0, y = 0;
  {
    title_ = create(tlines, tcols, y, x, /*bold=*/true);
    if (title_ == nullptr) return false;
    windows_[{NCursesWindow::kTitle, 0}] = title_;
    const std::string title_text = "Peregrine Integration Test";
    print(title_, 0,
          std::max(0, (tcols - static_cast<int>(title_text.size())) / 2),
          title_text);
    y += tlines + clines;

    hsplit_ = create(hlines, hcols, y, x);
    if (hsplit_ == nullptr) return false;
    windows_[{NCursesWindow::kHorizontalSplit, 0}] = hsplit_;
    for (int j = 0; j < getmaxx(hsplit_); ++j) {
      mvwprintw(hsplit_, hlines / 2, j, "-");
    }

    y = all_lines - qlines;
    stats_ = create(qlines, scols, y, x, /*bold=*/true);
    if (stats_ == nullptr) return false;
    windows_[{NCursesWindow::kStats, 0}] = stats_;
    mvwprintw(stats_, qlines - 1, 0, "Stats: ");
    x += scols;

    progress_ = create(qlines, pcols, y, x, /*bold=*/true);
    if (progress_ == nullptr) return false;
    windows_[{NCursesWindow::kProgress, 0}] = progress_;
    mvwprintw(progress_, qlines - 1, 0, "Progress: ");
  }

  // Top half: controlpath windows.
  x = spcols;
  y = tlines;
  {
    scp_ = create(clines, scols_side, y, x);
    if (scp_ == nullptr) return false;
    windows_[{NCursesWindow::kSenderControlpath, 0}] = scp_;
    print(scp_, 0, 0, "sender controlpath");
    x += scols_side + spcols;

    cvsplit_ = create(clines, vcols, y, x);
    if (cvsplit_ == nullptr) return false;
    windows_[{NCursesWindow::kVerticalControlSplit, 0}] = cvsplit_;
    for (int k = 0; k < getmaxy(cvsplit_); ++k) {
      mvwprintw(cvsplit_, k, vcols / 2, "|");
    }
    x += vcols + spcols;

    rcp_ = create(clines, rcols_side, y, x);
    if (rcp_ == nullptr) return false;
    windows_[{NCursesWindow::kReceiverControlpath, 0}] = rcp_;
    print(rcp_, 0, 0, "receiver controlpath");
  }

  // Bottom half: datapath windows.
  x = spcols;
  y = tlines + clines + hlines;
  {
    sdp_ = create(dlines, scols_side, y, x);
    if (sdp_ == nullptr) return false;
    windows_[{NCursesWindow::kSenderDatapath, 0}] = sdp_;
    print(sdp_, 0, 0, "sender datapath");
    x += scols_side + spcols;

    dvsplit_ = create(dlines, vcols, y, x);
    if (dvsplit_ == nullptr) return false;
    windows_[{NCursesWindow::kVerticalDataSplit, 0}] = dvsplit_;
    for (int k = 0; k < getmaxy(dvsplit_); ++k) {
      mvwprintw(dvsplit_, k, vcols / 2, "|");
    }
    x += vcols + spcols;

    rdp_ = create(dlines, rcols_side, y, x);
    if (rdp_ == nullptr) return false;
    windows_[{NCursesWindow::kReceiverDatapath, 0}] = rdp_;
    print(rdp_, 0, 0, "receiver datapath");
  }

  return true;
}

void NCurses::runWindows(std::function<int(WINDOW*)> func) {
  for (const auto& [unused, win] : windows_) {
    DCHECK_NE(win, nullptr);
    func(win);
  }
}

WINDOW* NCurses::create(int nlines, int ncols, int y, int x, bool bold) {
  WINDOW* win = newwin(nlines, ncols, y, x);
  if (win != nullptr && bold) {
    wstandout(win);
  }
  return win;
}

void NCurses::Print(NCursesWindow win, int id, std::string s) {
  switch (win) {
    case NCursesWindow::kStats:
    case NCursesWindow::kProgress:
      print(windows_[{win, 0}], 1, 0, s);
      break;
    case NCursesWindow::kSenderControlpath:
    case NCursesWindow::kReceiverControlpath:
    case NCursesWindow::kSenderDatapath:
    case NCursesWindow::kReceiverDatapath:
      print(windows_[{win, id}], 0, 0, s);
      break;
    case NCursesWindow::kTitle:
    case NCursesWindow::kHorizontalSplit:
    case NCursesWindow::kVerticalControlSplit:
    case NCursesWindow::kVerticalDataSplit:
    default:
      break;
  }
}

}  // namespace peregrine::integration
