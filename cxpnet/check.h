#ifndef CXPNET_CHECK_H
#define CXPNET_CHECK_H

#include <cassert>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef NDEBUG
#define CXPNET_CHECK(condition, fmt_str, ...)                                \
  do {                                                                       \
    const bool cxpnet_check_result = static_cast<bool>(condition);           \
    if (!cxpnet_check_result) {                                              \
      std::string cxpnet_check_msg =                                         \
          std::format((fmt_str) __VA_OPT__(, ) __VA_ARGS__);                 \
      std::cerr << "[CXPNET] CHECK failed: " << cxpnet_check_msg             \
                << " | Condition failed: " << #condition                    \
                << " | File: " << __FILE__                                  \
                << " | Line: " << __LINE__ << std::endl;                    \
      throw std::runtime_error(cxpnet_check_msg);                            \
    }                                                                        \
  } while (0)
#else
#define CXPNET_CHECK(condition, fmt_str, ...)                                \
  do {                                                                       \
    const bool cxpnet_check_result = static_cast<bool>(condition);           \
    if (!cxpnet_check_result) {                                              \
      std::string cxpnet_check_msg =                                         \
          std::format((fmt_str) __VA_OPT__(, ) __VA_ARGS__);                 \
      std::cerr << "[CXPNET] CHECK failed: " << cxpnet_check_msg             \
                << " | Condition failed: " << #condition                    \
                << " | File: " << __FILE__                                  \
                << " | Line: " << __LINE__ << std::endl;                    \
      assert(cxpnet_check_result);                                           \
    }                                                                        \
  } while (0)
#endif // NDEBUG

#endif // CXPNET_CHECK_H
