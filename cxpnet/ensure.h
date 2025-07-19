#ifndef ENSURE_H
#define ENSURE_H

#include <cassert>
#include <format>
#include <iostream>
#include <string>

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
      throw std::runtime_error(formatter_type);                          \
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
#endif // ENSURE_H