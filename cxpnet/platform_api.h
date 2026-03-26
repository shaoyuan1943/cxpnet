#ifndef PLATFORM_H
#define PLATFORM_H

#include "sock.h"

namespace cxpnet {
  // clang-format off
  enum class ErrorAction { kBreak, kContinue, kClose };
  // clang-format on

  // 统一的事件标志 (平台无关)
  // Poller 负责在平台事件和统一事件之间转换
  namespace events {
    static const int kNone  = 0;
    static const int kRead  = 1 << 0;   // 可读
    static const int kWrite = 1 << 1;   // 可写
    static const int kError = 1 << 2;   // 错误
    static const int kHup   = 1 << 3;   // 挂起/关闭
  } // namespace events

  class Platform {
  public:
    static void             close_handle(int fd);
    static bool             set_non_blocking(int fd);
    static int              get_last_error();
    static ErrorAction      handle_error_action(int err);
    static sockaddr_storage get_sockaddr(const char* address, uint16_t port, ProtocolStack stack);
    static int              listen(sockaddr_storage addr_storage, ProtocolStack proto_stack, int option);
    static int              accept(int listen_handle, std::vector<std::pair<int, sockaddr_storage>>& accepted_handles);
    static int              connect(sockaddr_storage addr_storage, bool async = true, uint32_t timeout_ms = 5000);
    static void             shut_wr(int fd);

    // wakeup 机制: Linux 使用 eventfd, macOS 使用 pipe
    static int  create_wakeup_fd();         // 创建 wakeup fd，返回写端
    static int  get_wakeup_read_fd(int fd); // macOS 需要，Linux 返回相同值
    static void destroy_wakeup_fd(int fd);
    static void wakeup_write(int fd);
    static void wakeup_read(int fd);
  };

} // namespace cxpnet

#endif // PLATFORM_H
