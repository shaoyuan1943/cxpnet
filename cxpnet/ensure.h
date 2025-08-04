#ifndef ENSURE_H
#define ENSURE_H

#include <cassert>
#include <format>
#include <iostream>
#include <string>
#include <chrono>
#include <mutex>

// in Visual Studio, NDEBUG will defined auto in Release
// in G++/Clang, must set compiler params -DNDEBUG in Release by user manual
#ifdef NDEBUG
#define ENSURE(condition, fmt_str, ...)                                  \
  do {                                                                   \
    if (!(condition)) {                                                  \
      std::string formatted_msg = std::format((fmt_str), ##__VA_ARGS__); \
      std::cerr << "[CXPNET] ERROR: " << formatted_msg                   \
                << " | Condition failed: " << #condition                 \
                << " | File: " << __FILE__                               \
                << " | Line: " << __LINE__ << std::endl;                 \
      throw std::runtime_error(formatted_msg);                           \
    }                                                                    \
  } while (0)
#else
#define ENSURE(condition, fmt_str, ...)                               \
  do {                                                                \
    if (!(condition)) {                                               \
      std::string assert_msg = std::format((fmt_str), ##__VA_ARGS__); \
      assert((condition) && assert_msg.c_str());                      \
    }                                                                 \
  } while (0)
#endif // NDEBUG

namespace {
  std::mutex log_mutex;

  template <typename... Args>
  void _log_impl(std::format_string<Args...> fmt, Args&&... args) {
    auto now = std::chrono::system_clock::now();
    std::string output = std::format("[{:%Y-%m-%d %H:%M:%S}] ", now);
    output += std::format(fmt, std::forward<Args>(args)...);

    std::lock_guard<std::mutex> lock(log_mutex);
    std::cout << output << std::endl;
  }
} // namespace


#ifndef NDEBUG
#define LOG_DEBUG(fmt, ...) _log_impl(fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) ((void)0)
#endif
#endif // ENSURE_H