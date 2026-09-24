#ifndef PEREGRINE_SRC_UTIL_THREAD_H_
#define PEREGRINE_SRC_UTIL_THREAD_H_

#include <string>
#include <thread>  // NOLINT

namespace peregrine::util {

using Thread = std::thread;
using Jthread = std::jthread;

// Yields the current thread to the kernel scheduler.
inline void Yield() { std::this_thread::yield(); }

// Returns the thread id where this function is called.
std::string ThreadId();

}  // namespace peregrine::util

#endif  // PEREGRINE_SRC_UTIL_THREAD_H_
