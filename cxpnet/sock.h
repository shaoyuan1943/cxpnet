#ifndef IO_DEF_H
#define IO_DEF_H

// 平台检测宏
#if defined(__linux__)
  #define CXP_PLATFORM_LINUX 1
#elif defined(__APPLE__)
  #define CXP_PLATFORM_MACOS 1
#else
  #error "Unsupported platform: cxpnet only supports Linux and macOS"
#endif

// 平台无关头文件
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <memory>
#include <regex>
#include <string>

namespace cxpnet {
  class Conn;
  using socket_t                           = int;
  using SOCKET                             = socket_t;
  static constexpr socket_t invalid_socket = -1;
  static constexpr int      SOCKET_ERROR   = -1;

  using ConnPtr = std::shared_ptr<Conn>;
  using Closure = std::function<void()>;

  namespace SocketOption {
    static const int kNone      = 0;
    static const int kReusePort = 1 << 0;
    static const int kReuseAddr = 1 << 1;
  } // namespace SocketOption

  static constexpr size_t   kMaxPollEventCount = 64;
  static constexpr uint32_t kPollTimeoutMS     = 10000;
  // clang-format off
  enum class ProtocolStack { kIPv4Only, kIPv6Only, kDualStack };
  enum class IPType { kInvalid, kIPv4, kIPv6 };
  enum class RunningMode { kOnePollPerThread, kAllOneThread };
  enum class State { kDisconnected, kConnecting, kConnected, kDisconnecting };
  // clang-format on
 
  inline IPType ip_address_type(const std::string& address) {
    if (address.empty()) { return IPType::kInvalid; }

    sockaddr_in sa {};
    if (inet_pton(AF_INET, address.c_str(), &(sa.sin_addr)) == 1) {
      return IPType::kIPv4;
    }

    sockaddr_in6 sa6 {};
    if (inet_pton(AF_INET6, address.c_str(), &(sa6.sin6_addr)) == 1) {
      return IPType::kIPv6;
    }

    return IPType::kInvalid;
  }

  class NonCopyable {
  public:
    NonCopyable(const NonCopyable&)            = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
    NonCopyable(NonCopyable&&)                 = delete;
    NonCopyable& operator=(NonCopyable&&)      = delete;
  protected:
    NonCopyable()  = default;
    ~NonCopyable() = default;
  };
} // namespace cxpnet

#endif // IO_DEF_H