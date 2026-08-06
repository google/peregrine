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
  // Visual layout of the NCurses UI:
  // +------------------------------------------------------------+
  // |                          Title                             |
  // +------------------------------------------------------------+
  // |  [scp_]                      |                     [rcp_]  |
  // |  Sender Control Path     [cvsplit_]  Receiver Control Path |
  // |                              |                             |
  // +--------------------------[hsplit_]-------------------------+
  // |  [sdp_]                      |                     [rdp_]  |
  // |  Sender Data Path        [dvsplit_]   Receiver Data Path   |
  // |                              |                             |
  // +------------------------------------------------------------+
  // |  [stats_]                    |                [progress_]  |
  // +------------------------------------------------------------+
  WINDOW* stdscr_ = nullptr;    // Standard screen (main window)
  WINDOW* title_ = nullptr;     // Title bar window
  WINDOW* hsplit_ = nullptr;    // Horizontal splitter (control vs data)
  WINDOW* cvsplit_ = nullptr;   // Vertical splitter for controlpath
  WINDOW* dvsplit_ = nullptr;   // Vertical splitter for datapath
  WINDOW* stats_ = nullptr;     // Statistics window
  WINDOW* progress_ = nullptr;  // Progress window
  WINDOW* scp_ = nullptr;       // Sender Control Path window
  WINDOW* rcp_ = nullptr;       // Receiver Control Path window
  WINDOW* sdp_ = nullptr;       // Sender Data Path window
  WINDOW* rdp_ = nullptr;       // Receiver Data Path window

  absl::flat_hash_map<std::pair<NCursesWindow, int>, WINDOW*> windows_;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_NCURSES_H_
