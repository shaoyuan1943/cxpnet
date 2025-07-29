#ifndef PLATFORM_H
#define PLATFORM_H

#include "base_types_value.h"

namespace cxpnet {
  // clang-format off
  enum class ErrorAction { kBreak, kContinue, kClose };
  // clang-format on

  class Platform {
  public:
    static void             close_handle(int fd);
    static bool             set_non_blocking(int fd);
    static int              get_last_error();
    static ErrorAction      handle_error_action(int err);
    static sockaddr_storage get_sockaddr(const char* address, uint16_t port, ProtocolStack stack);
    static int              listen(sockaddr_storage addr_storage, ProtocolStack proto_stack, int option);
    static int              accept(int listen_handle, std::vector<std::pair<int, sockaddr_storage>>& accepted_handles);
    static int              connect(sockaddr_storage addr_storage, bool async = true);
#ifdef __linux__
    static void shut_wr(int fd);
    static void write_to_fd(int fd);
    static void read_from_fd(int fd);
    static int  create_event_fd();

    class events {
    public:
      static const int kNone  = 0;
      static const int kRead  = EPOLLIN | EPOLLRDHUP;
      static const int kWrite = EPOLLOUT;
    };
#endif // __linux__
  };
} // namespace cxpnet

#endif // PLATFORM_H