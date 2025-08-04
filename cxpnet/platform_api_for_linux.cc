#include "platform_api.h"
#include "base_types_value.h"
#include "ensure.h"

#ifdef __linux__
namespace cxpnet {
  void Platform::close_handle(int fd) { close(fd); }

  bool Platform::set_non_blocking(int fd) {
    int option = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, option | O_NONBLOCK) == 0;
  }

  int Platform::get_last_error() { return errno;}

  ErrorAction Platform::handle_error_action(int err) {
    switch (err) {
    case EAGAIN: // EAGIN == EWOULDBLOCK
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

    // // supports both IPv4 and IPv6 simultaneously
    if (addr_storage.ss_family == AF_INET6 && proto_stack == ProtocolStack::kDualStack) {
      int ipv6_only = 0;
      if (::setsockopt(handle, IPPROTO_IPV6, IPV6_V6ONLY, &ipv6_only, sizeof(ipv6_only)) == SOCKET_ERROR) {
        if (ipv6_only == 0) {
          close_handle(handle);
          return invalid_socket;
        }
      }
    }

    if ((option & SocketOption::kReuseAddr) == SocketOption::kReuseAddr) {
      int reuse_addr = 1;
      if (::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) == SOCKET_ERROR) {
        close_handle(handle);
        return invalid_socket;
      }
    }

    if ((option & SocketOption::kReusePort) == SocketOption::kReusePort) {
      int reuse_port = 1;
      if (::setsockopt(handle, SOL_SOCKET, SO_REUSEPORT, &reuse_port, sizeof(reuse_port)) == SOCKET_ERROR) {
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
      int              handle              = ::accept4(listen_handle,
                                                       reinterpret_cast<sockaddr*>(&remote_addr_storage),
                                                       &addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
      if (handle == invalid_socket) {
        int         err    = get_last_error();
        ErrorAction action = handle_error_action(err);
        if (action == ErrorAction::kBreak) { break; }
        if (action == ErrorAction::kContinue) { continue; }
        return err;
      }

      accepted_handles.push_back(std::make_pair(handle, remote_addr_storage));
    }

    return 0;
  }

  int Platform::connect(sockaddr_storage addr_storage, bool async) {
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

      timeval timeout {5, 0};
      result = ::select((int)(handle + 1), nullptr, &write_set, nullptr, &timeout);

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

  void Platform::write_to_fd(int fd) {
    uint64_t one = 1;
    ssize_t  n   = write(fd, &one, sizeof(one));
    ENSURE(n == sizeof(one), "write to fd failed in write_fd");
  }
  
  void Platform::read_from_fd(int fd) {
    uint64_t one = 1;
    ssize_t  n   = read(fd, &one, sizeof(one));
    ENSURE(n == sizeof(one), "write to fd failed in read_fd");
  }

  int Platform::create_event_fd() {
    int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    return fd;
  }
}
#endif // __linux__