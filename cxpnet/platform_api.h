#ifndef PLATFORM_H
#define PLATFORM_H

#include "sock.h"
#include <cerrno>
#include <vector>

namespace cxpnet {
  // clang-format off
  enum class ErrorAction { kBreak, kContinue, kClose };
  // clang-format on

  namespace errors {
#if defined(CXPNET_PLATFORM_WINDOWS)
    inline constexpr int kInvalidArgument = WSAEINVAL;
    inline constexpr int kTimedOut        = WSAETIMEDOUT;
    inline constexpr int kTooManyFiles    = WSAEMFILE;
    inline constexpr int kInterrupted     = WSAEINTR;
    inline constexpr int kCanceled        = WSA_OPERATION_ABORTED;
    inline constexpr int kBadHandle       = WSAENOTSOCK;
#else
    inline constexpr int kInvalidArgument = EINVAL;
    inline constexpr int kTimedOut        = ETIMEDOUT;
    inline constexpr int kTooManyFiles    = EMFILE;
    inline constexpr int kInterrupted     = EINTR;
    inline constexpr int kCanceled        = ECANCELED;
    inline constexpr int kBadHandle       = EBADF;
#endif
  } // namespace errors

  struct WakeupHandles {
    socket_t read  = invalid_socket;
    socket_t write = invalid_socket;
  };

  // 统一的事件标志 (平台无关)
  // Poller 负责在平台事件和统一事件之间转换
  namespace events {
    static const int kNone  = 0;
    static const int kRead  = 1 << 0; // 可读
    static const int kWrite = 1 << 1; // 可写
    static const int kError = 1 << 2; // 错误
    static const int kHup   = 1 << 3; // 挂起/关闭
  } // namespace events

  class Platform {
  public:
    static void             close_handle(socket_t fd);
    static bool             set_non_blocking(socket_t fd);
    static int              get_last_error();
    static ErrorAction      handle_error_action(int err);
    static sockaddr_storage get_sockaddr(const char* address, uint16_t port, ProtocolStack stack);
    static socket_t         listen(sockaddr_storage addr_storage, ProtocolStack proto_stack, int option);
    static int              accept(socket_t listen_handle, std::vector<std::pair<socket_t, sockaddr_storage>>& accepted_handles);
    static socket_t         connect(sockaddr_storage addr_storage, bool async = true, uint32_t timeout_ms = 5000);
    static int              send(socket_t fd, const char* data, size_t size);
    static int              recv(socket_t fd, char* data, size_t size);
    static int              get_socket_error(socket_t fd);
    static void             shut_wr(socket_t fd);

    // Linux 的 eventfd 共用读写端；Windows 使用 loopback TCP socket 对。
    static WakeupHandles create_wakeup();
    static void          destroy_wakeup(WakeupHandles handles);
    static void          wakeup_write(socket_t fd);
    static void          wakeup_read(socket_t fd);
  };

} // namespace cxpnet

#endif // PLATFORM_H
