#ifndef IO_DEF_H
#define IO_DEF_H

#include <functional>
#include <memory>

#ifdef _WIN32

#include <MSWSock.h>
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <Windows.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46 // Standard IPv6 address string length
#endif

#else

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>

#define _GNU_SOURCE // for accept4
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/event.h>
#endif

#ifdef __linux__
#include <sys/epoll.h>
#endif

#endif

#include <regex>
#include <string>
#include <iostream>

namespace cxpnet {
  class Conn;
  class Socket;
#ifdef _WIN32
  using socket_t                           = SOCKET;
  static constexpr socket_t invalid_socket = INVALID_SOCKET;
#else
  using socket_t                           = int;
  using SOCKET                             = socket_t;
  static constexpr socket_t invalid_socket = -1;
  static constexpr int      SOCKET_ERROR   = -1;
#endif // _WIN32
  using OnConnCallback = std::function<void(std::shared_ptr<Conn>)>;
  using OnConnClosedCallback = std::function<void(std::shared_ptr<Conn>, int)>;
  using OnDataCallback       = std::function<void(std::shared_ptr<Conn>, const char*, size_t)>;
  using OnPollerErrorCallback = std::function<void(int)>;



  

  // socket read and write buffer size
  static constexpr size_t max_read_buff_size  = 1024 * 4;
  static constexpr size_t max_write_buff_size = 1024 * 4;

  // every io operation size
  static constexpr size_t max_size_per_write = 1024 * 2;
  static constexpr size_t max_size_per_read  = 1024 * 2;

  static constexpr size_t max_epoll_event_count = 64;

  enum class IPType { kInvalid,
                      kIPv4,
                      kIPv6 };
  inline IPType ip_address_type(const std::string& address) {
    if (address.empty()) {
      return IPType::kInvalid;
    }

    sockaddr_in sa = {0};
    if (inet_pton(AF_INET, address.c_str(), &(sa.sin_addr)) == 1) {
      return IPType::kIPv4;
    }

    sockaddr_in6 sa6 = {0};
    if (inet_pton(AF_INET6, address.c_str(), &(sa6.sin6_addr)) == 1) {
      return IPType::kIPv6;
    }

    return IPType::kInvalid;
  }

  // clang-format off
  enum class ProtocolStack { kIPv4Only, kIPv6Only, kDualStack };
  // clang-format on
  namespace SocketOption {
    static const int kNone = 0;
    static const int kReusePort = 1 << 0;
    static const int kReuseAddr = 1 << 1;
  }

  static const uint32_t kPollTimeoutMS = 10000;
  enum class RunningMode { kOnePollPerThread, kAllOneThread };
} // namespace coxnet

#endif // IO_DEF_H