#ifndef PEREGRINE_TEST_INTEGRATION_NCURSES_H_
#define PEREGRINE_TEST_INTEGRATION_NCURSES_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include <ncurses.h>

namespace peregrine::integration {

enum class NCursesWindow : uint16_t {
  kTitle,
  kHorizontalSplit,
  kVerticalControlSplit,
  kVerticalDataSplit,
  kStats,
  kProgress,
  kSenderControlpath,
  kReceiverControlpath,
  kSenderDatapath,
  kReceiverDatapath,
  kOther,
};

class NCurses final {
 public:
  // Creates a unique_ptr of NCurses.
  static absl::StatusOr<std::unique_ptr<NCurses>> Create();

  // Destructor.
  ~NCurses();

  // Prints a string in a window.
  void Print(NCursesWindow win, std::string s) { Print(win, 0, std::move(s)); }
  void Print(NCursesWindow win, int id, std::string s);

  // Refreshes all the windows.
  void Refresh() { runWindows(wrefresh); }

 private:
  static constexpr int kScreenMinNumLines = 24;
  static constexpr int kScreenMinNumCols = 80;

  // Default constructor.
  NCurses() = default;

  // Initializes ncurses.
  absl::Status init();

  // Resets ncurses.
  void reset();

  // Returns true iff the terminal screen size is big enough.
  bool isScreenBigEnough() const {
    return getmaxy(stdscr_) >= kScreenMinNumLines &&
           getmaxx(stdscr_) >= kScreenMinNumCols;
  }

  // Initializes all the windows.
  bool initWindows();

  // Resets all the windows.
  void resetWindows() { runWindows(delwin); }

  // Runs a function for all the windows.
  void runWindows(std::function<int(WINDOW*)> func);

  // Creates a window.
  WINDOW* create(int nlines, int ncols, int y, int x, bool bold = false);

  // Prints a string in a window.
  void print(WINDOW* win, int y, int x, std::string s) {
    if (win != nullptr) {
      mvwprintw(win, y, x, "%s", s.c_str());
      wclrtoeol(win);
    }
  }

 private:
  WINDOW* stdscr_ = nullptr;
  WINDOW* title_ = nullptr;
  WINDOW* hsplit_ = nullptr;
  WINDOW* cvsplit_ = nullptr;
  WINDOW* dvsplit_ = nullptr;
  WINDOW* stats_ = nullptr;
  WINDOW* progress_ = nullptr;
  WINDOW* scp_ = nullptr;
  WINDOW* rcp_ = nullptr;
  WINDOW* sdp_ = nullptr;
  WINDOW* rdp_ = nullptr;

  absl::flat_hash_map<std::pair<NCursesWindow, int>, WINDOW*> windows_;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_NCURSES_H_
