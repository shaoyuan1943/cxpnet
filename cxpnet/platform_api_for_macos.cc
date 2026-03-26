#include "ensure.h"
#include "platform_api.h"
#include "sock.h"

#include <sys/event.h>

#include <unordered_map>

namespace cxpnet {
  // macOS wakeup fd 映射：写端 fd -> 读端 fd
  // 使用 unordered_map 存储映射，避免 pipe fd 不连续假设
  static std::unordered_map<int, int> g_wakeup_fd_map;
  static std::mutex                   g_wakeup_fd_map_mutex;
  void                                Platform::close_handle(int fd) { close(fd); }

  bool Platform::set_non_blocking(int fd) {
    int option = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, option | O_NONBLOCK) == 0;
  }

  int Platform::get_last_error() { return errno; }

  ErrorAction Platform::handle_error_action(int err) {
    switch (err) {
    case EAGAIN: // EAGAIN == EWOULDBLOCK
      return ErrorAction::kBreak;
    case EPROTO:
    case ECONNABORTED:
    case EINTR:
      return ErrorAction::kContinue;
    default: // EMFILE, ENFILE, EBADF, EINVAL, EFAULT
      break;
    }

    return ErrorAction::kClose;
  }

  sockaddr_storage Platform::get_sockaddr(const char* address, uint16_t port, ProtocolStack stack) {
    sockaddr_storage addr_storage = {};

    IPType ip_type = ip_address_type(std::string(address));
    if (ip_type == IPType::kInvalid) { return addr_storage; }

    int af_family = 0;
    if (ip_type == IPType::kIPv4 && stack == ProtocolStack::kIPv4Only) {
      af_family = AF_INET;
    }

    if (ip_type == IPType::kIPv6 && stack == ProtocolStack::kIPv6Only) {
      af_family = AF_INET6;
    }

    if (ip_type == IPType::kIPv6 && stack == ProtocolStack::kDualStack) {
      af_family = AF_INET6; // supports both IPv4 and IPv6 simultaneously
    }

    if (af_family == 0) { return addr_storage; }

    if (af_family == AF_INET) {
      sockaddr_in* sockaddr_info = reinterpret_cast<sockaddr_in*>(&addr_storage);
      sockaddr_info->sin_family  = AF_INET;
      sockaddr_info->sin_port    = htons(port);
      if (inet_pton(AF_INET, address, &sockaddr_info->sin_addr) <= 0) {
        addr_storage.ss_family = 0;
        return addr_storage;
      }
    }

    if (af_family == AF_INET6) {
      sockaddr_in6* sockaddr_info6 = reinterpret_cast<sockaddr_in6*>(&addr_storage);
      sockaddr_info6->sin6_family  = AF_INET6;
      sockaddr_info6->sin6_port    = htons(port);
      if (inet_pton(AF_INET6, address, &sockaddr_info6->sin6_addr) <= 0) {
        addr_storage.ss_family = 0;
        return addr_storage;
      }
    }

    return addr_storage;
  }

  int Platform::listen(sockaddr_storage addr_storage, ProtocolStack proto_stack, int option) {
    if (addr_storage.ss_family == 0) { return invalid_socket; }

    int handle = ::socket(addr_storage.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (handle == invalid_socket) { return handle; }

    // supports both IPv4 and IPv6 simultaneously
    if (addr_storage.ss_family == AF_INET6 && proto_stack == ProtocolStack::kDualStack) {
      int ipv6_only = 0;
      if (::setsockopt(handle, IPPROTO_IPV6, IPV6_V6ONLY, &ipv6_only, sizeof(ipv6_only)) == SOCKET_ERROR) {
        if (ipv6_only == 0) {
          close_handle(handle);
          return invalid_socket;
        }
      }
    }

    // macOS 没有 SO_REUSEPORT，使用 SO_REUSEADDR 替代
    if ((option & SocketOption::kReuseAddr) == SocketOption::kReuseAddr) {
      int reuse_addr = 1;
      if (::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) == SOCKET_ERROR) {
        close_handle(handle);
        return invalid_socket;
      }
    }

    if (!set_non_blocking(handle)) {
      close_handle(handle);
      return invalid_socket;
    }

    socklen_t addr_len = 0;
    if (addr_storage.ss_family == AF_INET) { addr_len = sizeof(sockaddr_in); }
    if (addr_storage.ss_family == AF_INET6) { addr_len = sizeof(sockaddr_in6); }

    if (::bind(handle, reinterpret_cast<sockaddr*>(&addr_storage), addr_len) == SOCKET_ERROR) {
      close_handle(handle);
      return invalid_socket;
    }

    if (::listen(handle, SOMAXCONN) == SOCKET_ERROR) {
      close_handle(handle);
      return invalid_socket;
    }

    return handle;
  }

  int Platform::accept(int listen_handle, std::vector<std::pair<int, struct sockaddr_storage>>& accepted_handles) {
    if (listen_handle == -1) { return -1; }

    while (true) {
      sockaddr_storage remote_addr_storage = {};
      socklen_t        addr_len            = sizeof(remote_addr_storage);
      // macOS 没有 accept4，使用 accept + fcntl
      int handle = ::accept(listen_handle,
                            reinterpret_cast<sockaddr*>(&remote_addr_storage),
                            &addr_len);
      if (handle == invalid_socket) {
        int         err    = get_last_error();
        ErrorAction action = handle_error_action(err);
        if (action == ErrorAction::kBreak) { break; }
        if (action == ErrorAction::kContinue) { continue; }
        return err;
      }

      // 设置非阻塞
      if (!set_non_blocking(handle)) {
        // set_non_blocking 失败，关闭 socket 并继续循环
        close_handle(handle);
        continue;
      }

      accepted_handles.push_back(std::make_pair(handle, remote_addr_storage));
    }

    return 0;
  }

  int Platform::connect(sockaddr_storage addr_storage, bool async, uint32_t timeout_ms) {
    if (addr_storage.ss_family == 0) { return -1; }

    int handle = socket(addr_storage.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (handle == invalid_socket) { return -1; }

    if (!set_non_blocking(handle)) {
      close_handle(handle);
      return -1;
    }

    size_t addr_len = 0;
    if (addr_storage.ss_family == AF_INET) { addr_len = sizeof(sockaddr_in); }
    if (addr_storage.ss_family == AF_INET6) { addr_len = sizeof(sockaddr_in6); }

    // EINPROGRESS is mean of async operation is in progress, ignore this error code
    int result = ::connect(handle, reinterpret_cast<sockaddr*>(&addr_storage), addr_len);
    if (result == 0) { return handle; }

    if (get_last_error() != EINPROGRESS) {
      close_handle(handle);
      return invalid_socket;
    }

    if (!async) {
      fd_set write_set;
      FD_ZERO(&write_set);
      FD_SET(handle, &write_set);

      timeval timeout;
      timeout.tv_sec  = timeout_ms / 1000;
      timeout.tv_usec = (timeout_ms % 1000) * 1000;
      result          = ::select((int)(handle + 1), nullptr, &write_set, nullptr, &timeout);

      if (result != 1) {
        close_handle(handle);
        return invalid_socket;
      }

      int       err = 0;
      socklen_t len = sizeof(err);
      if (getsockopt(handle, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
        close_handle(handle);
        return invalid_socket;
      }
    }

    return handle;
  }

  void Platform::shut_wr(int fd) {
    ::shutdown(fd, SHUT_WR);
  }

  // macOS 使用 pipe 实现 wakeup
  // 返回写端 fd，读端通过 get_wakeup_read_fd 获取
  int Platform::create_wakeup_fd() {
    int fds[2];
    if (pipe(fds) < 0) {
      return invalid_socket;
    }
    // fds[0] 是读端，fds[1] 是写端
    set_non_blocking(fds[0]);
    set_non_blocking(fds[1]);

    // 存储映射关系：写端 -> 读端
    {
      std::lock_guard<std::mutex> lock(g_wakeup_fd_map_mutex);
      g_wakeup_fd_map[fds[1]] = fds[0];
    }

    // 返回写端
    return fds[1];
  }

  // macOS pipe 读端需要单独获取
  int Platform::get_wakeup_read_fd(int write_fd) {
    std::lock_guard<std::mutex> lock(g_wakeup_fd_map_mutex);
    auto                        it = g_wakeup_fd_map.find(write_fd);
    if (it != g_wakeup_fd_map.end()) {
      return it->second;
    }
    // 未找到映射，返回无效 fd
    return invalid_socket;
  }

  void Platform::destroy_wakeup_fd(int write_fd) {
    if (write_fd < 0) { return; }

    int read_fd = invalid_socket;
    {
      std::lock_guard<std::mutex> lock(g_wakeup_fd_map_mutex);
      auto                        it = g_wakeup_fd_map.find(write_fd);
      if (it != g_wakeup_fd_map.end()) {
        read_fd = it->second;
        g_wakeup_fd_map.erase(it);
      }
    }

    if (read_fd >= 0) {
      close_handle(read_fd);
    }
    close_handle(write_fd);
  }

  void Platform::wakeup_write(int fd) {
    char    c = 'x';
    ssize_t n = ::write(fd, &c, sizeof(c));
    // EAGAIN 是正常的，说明已经有唤醒信号在等待
    if (n < 0 && errno != EAGAIN) {
      ENSURE(false, "wakeup_write failed");
    }
  }

  void Platform::wakeup_read(int fd) {
    char buf[256];
    // 读取所有数据
    while (true) {
      ssize_t n = ::read(fd, buf, sizeof(buf));
      if (n < 0) {
        if (errno == EAGAIN) {
          break;
        }
        ENSURE(false, "wakeup_read failed");
      }
      if (n == 0) {
        break;
      }
    }
  }
} // namespace cxpnet
