#include "check.h"
#include "platform_api.h"

#include <algorithm>
#include <climits>
#include <stdexcept>

namespace cxpnet {
  namespace {
    void ensure_winsock_env_() {
      struct WinsockRuntime {
        WinsockRuntime() {
          WSADATA data {};
          int     err = WSAStartup(MAKEWORD(2, 2), &data);
          if (err != 0) {
            throw std::runtime_error(std::format("WSAStartup failed: {}", err));
          }
        }
        ~WinsockRuntime() { WSACleanup(); }
      };

      static WinsockRuntime runtime;
    }

    socket_t close_failed_socket_(socket_t fd, int err) {
      if (fd != invalid_socket) {
        ::closesocket(fd);
      }

      WSASetLastError(err);
      return invalid_socket;
    }

    int address_size_(const sockaddr_storage& addr) {
      return addr.ss_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
    }
  } // namespace

  void Platform::close_handle(socket_t fd) {
    ::closesocket(fd);
  }

  bool Platform::set_non_blocking(socket_t fd) {
    u_long enabled = 1;
    return ::ioctlsocket(fd, FIONBIO, &enabled) == 0;
  }

  int Platform::get_last_error() {
    return WSAGetLastError();
  }

  ErrorAction Platform::handle_error_action(int err) {
    if (err == WSAEWOULDBLOCK) { return ErrorAction::kBreak; }
    if (err == WSAEINTR) { return ErrorAction::kContinue; }
    return ErrorAction::kClose;
  }

  sockaddr_storage Platform::get_sockaddr(const char* address, uint16_t port, ProtocolStack stack) {
    ensure_winsock_env_();

    sockaddr_storage storage {};
    IPType           type = ip_address_type(address);
    if (type == IPType::kIPv4 && stack == ProtocolStack::kIPv4Only) {
      auto* addr       = reinterpret_cast<sockaddr_in*>(&storage);
      addr->sin_family = AF_INET;
      addr->sin_port   = htons(port);
      ::inet_pton(AF_INET, address, &addr->sin_addr);
    } else if (type == IPType::kIPv6 && stack != ProtocolStack::kIPv4Only) {
      auto* addr        = reinterpret_cast<sockaddr_in6*>(&storage);
      addr->sin6_family = AF_INET6;
      addr->sin6_port   = htons(port);
      ::inet_pton(AF_INET6, address, &addr->sin6_addr);
    }

    return storage;
  }

  socket_t Platform::listen(sockaddr_storage storage, ProtocolStack stack, int option) {
    ensure_winsock_env_();

    if (storage.ss_family != AF_INET && storage.ss_family != AF_INET6) {
      WSASetLastError(WSAEINVAL);
      return invalid_socket;
    }

    if (option & SocketOption::kReusePort) {
      WSASetLastError(WSAEOPNOTSUPP);
      return invalid_socket;
    }

    socket_t fd = ::socket(storage.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (fd == invalid_socket) { return fd; }

    if (storage.ss_family == AF_INET6 && stack == ProtocolStack::kDualStack) {
      int only_ipv6 = 0;
      if (::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
                       reinterpret_cast<const char*>(&only_ipv6), sizeof(only_ipv6)) == SOCKET_ERROR) {
        return close_failed_socket_(fd, WSAGetLastError());
      }
    }

    if (option & SocketOption::kReuseAddr) {
      int reuse = 1;
      if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                       reinterpret_cast<const char*>(&reuse), sizeof(reuse)) == SOCKET_ERROR) {
        return close_failed_socket_(fd, WSAGetLastError());
      }
    }

    if (!set_non_blocking(fd) || ::bind(fd, reinterpret_cast<sockaddr*>(&storage), address_size_(storage)) == SOCKET_ERROR || ::listen(fd, SOMAXCONN) == SOCKET_ERROR) {
      return close_failed_socket_(fd, WSAGetLastError());
    }

    return fd;
  }

  int Platform::accept(socket_t listener, std::vector<std::pair<socket_t, sockaddr_storage>>& accepted) {
    while (true) {
      sockaddr_storage addr {};
      int              size = sizeof(addr);
      socket_t         fd   = ::accept(listener, reinterpret_cast<sockaddr*>(&addr), &size);
      if (fd == invalid_socket) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) { return 0; }
        if (err == WSAEINTR || err == WSAECONNRESET || err == WSAECONNABORTED) { continue; }

        // accept 出错意味着后面都可能是错的，将这一批的 handle 全部关掉
        for (auto& entry : accepted) { close_handle(entry.first); }

        accepted.clear();
        return err;
      }

      if (!set_non_blocking(fd)) {
        close_handle(fd);
        continue;
      }

      accepted.emplace_back(fd, addr);
    }
  }

  socket_t Platform::connect(sockaddr_storage storage, bool async, uint32_t timeout_ms) {
    ensure_winsock_env_();

    if (storage.ss_family != AF_INET && storage.ss_family != AF_INET6) {
      WSASetLastError(WSAEINVAL);
      return invalid_socket;
    }

    socket_t fd = ::socket(storage.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (fd == invalid_socket) { return fd; }

    if (!set_non_blocking(fd)) { return close_failed_socket_(fd, WSAGetLastError()); }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&storage), address_size_(storage)) == 0) { return fd; }

    int err = WSAGetLastError();
    if (err != WSAEWOULDBLOCK) { return close_failed_socket_(fd, err); }

    if (async) { return fd; }

    // 单 socket 的同步等待无需 fd_set 容量管理，同时检查连接失败集合
    fd_set writes;
    fd_set errors;
    FD_ZERO(&writes);
    FD_ZERO(&errors);
    FD_SET(fd, &writes);
    FD_SET(fd, &errors);

    timeval timeout {static_cast<long>(timeout_ms / 1000), static_cast<long>((timeout_ms % 1000) * 1000)};
    int     result = ::select(0, nullptr, &writes, &errors, &timeout);
    if (result == SOCKET_ERROR) {
      return close_failed_socket_(fd, WSAGetLastError());
    }

    if (result == 0) {
      return close_failed_socket_(fd, WSAETIMEDOUT);
    }

    err = get_socket_error(fd);
    if (err != 0) {
      return close_failed_socket_(fd, err);
    }

    return fd;
  }

  int Platform::send(socket_t fd, const char* data, size_t size) {
    return ::send(fd, data, static_cast<int>(std::min(size, static_cast<size_t>(INT_MAX))), 0);
  }

  int Platform::recv(socket_t fd, char* data, size_t size) {
    return ::recv(fd, data, static_cast<int>(std::min(size, static_cast<size_t>(INT_MAX))), 0);
  }

  int Platform::get_socket_error(socket_t fd) {
    int err  = 0;
    int size = sizeof(err);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &size) == SOCKET_ERROR) {
      return WSAGetLastError();
    }

    return err;
  }

  void Platform::shut_wr(socket_t fd) { ::shutdown(fd, SD_SEND); }

  WakeupHandles Platform::create_wakeup() {
    ensure_winsock_env_();

    WakeupHandles handles;
    socket_t      listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    auto          failed   = [&]() -> WakeupHandles {
      int err = WSAGetLastError();
      if (listener != invalid_socket) {
        close_handle(listener);
      }

      destroy_wakeup(handles);
      throw std::runtime_error(std::format("create wakeup sockets failed: {}", err));
    };

    if (listener == invalid_socket) { return failed(); }

    sockaddr_in addr {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int size             = sizeof(addr);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), size) == SOCKET_ERROR) {
      return failed();
    }

    if (::listen(listener, 1) == SOCKET_ERROR) {
      return failed();
    }

    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &size) == SOCKET_ERROR) {
      return failed();
    }

    sockaddr_storage storage {};
    std::memcpy(&storage, &addr, sizeof(addr));
    handles.write = connect(storage, false, 5000);
    if (handles.write == invalid_socket) { return failed(); }

    handles.read = ::accept(listener, nullptr, nullptr);
    if (handles.read == invalid_socket || !set_non_blocking(handles.read)) { return failed(); }

    int no_delay = 1;
    if (::setsockopt(handles.write, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char*>(&no_delay), sizeof(no_delay)) == SOCKET_ERROR) {
      return failed();
    }

    close_handle(listener);
    return handles;
  }

  void Platform::destroy_wakeup(WakeupHandles handles) {
    if (handles.read != invalid_socket) {
      close_handle(handles.read);
    }

    if (handles.write != invalid_socket && handles.write != handles.read) {
      close_handle(handles.write);
    }
  }

  void Platform::wakeup_write(socket_t fd) {
    const char byte = 1;
    while (::send(fd, &byte, 1, 0) == SOCKET_ERROR) {
      int err = WSAGetLastError();
      if (err == WSAEINTR) { continue; }
      if (err == WSAEWOULDBLOCK) { return; }

      CXPNET_CHECK(false, "wakeup_write failed: {}", err);
      return;
    }
  }

  void Platform::wakeup_read(socket_t fd) {
    char buffer[128];
    while (true) {
      int count = ::recv(fd, buffer, sizeof(buffer), 0);
      if (count > 0) { continue; }

      int err = count == 0 ? WSAECONNRESET : WSAGetLastError();
      if (err == WSAEINTR) { continue; }
      if (err == WSAEWOULDBLOCK) { return; }

      CXPNET_CHECK(false, "wakeup_read failed: {}", err);
      return;
    }
  }
} // namespace cxpnet
