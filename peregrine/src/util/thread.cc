#include "peregrine/src/util/thread.h"

#include <sstream>
#include <string>
#include <thread>  // NOLINT

namespace peregrine::util {

std::string ThreadId() {
  std::stringstream ss;
  ss << std::this_thread::get_id();
  return ss.str();
}

}  // namespace peregrine::util
