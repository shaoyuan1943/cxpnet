#include <cxpnet/cxpnet.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#if defined(CXPNET_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <pthread.h>
#endif
#include <string_view>
#include <thread>

namespace {
#if defined(CXPNET_PLATFORM_WINDOWS)
  volatile LONG stop_requests = 0;
  BOOL WINAPI handle_console_control(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) { return FALSE; }
    InterlockedIncrement(&stop_requests);
    return TRUE;
  }
#else
  sigset_t make_stop_signal_set() {
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    return signals;
  }
#endif
} // namespace

int main(int argc, char* argv[]) {
  const char* host = argc > 1 ? argv[1] : "127.0.0.1";
  uint16_t    port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : 9096;

#if defined(CXPNET_PLATFORM_WINDOWS)
  if (!SetConsoleCtrlHandler(handle_console_control, TRUE)) {
    std::cerr << "failed to install console handler" << std::endl;
    return 1;
  }
#else
  sigset_t stop_signals = make_stop_signal_set();
  if (pthread_sigmask(SIG_BLOCK, &stop_signals, nullptr) != 0) {
    std::cerr << "failed to block stop signals" << std::endl;
    return 1;
  }
#endif

  cxpnet::Server server(host, port, cxpnet::ProtocolStack::kIPv4Only, cxpnet::SocketOption::kReuseAddr);
  server.set_graceful_close_timeout(3000);
  server.set_conn_user_callback([](cxpnet::ConnPtr conn) {
    std::cout << "client connected: " << conn->remote_addr_and_port().first
              << ":" << conn->remote_addr_and_port().second << std::endl;

    std::weak_ptr<cxpnet::Conn> weak_conn = conn;
    conn->set_message_callback([weak_conn](std::string_view data) {
      if (auto conn = weak_conn.lock()) {
        conn->send(data);
      }
    });
    conn->set_close_callback([](int err) {
      std::cout << "client closed: " << err << std::endl;
    });
  });

  if (!server.start(cxpnet::RunningMode::kAllOneThread)) {
    std::cerr << "failed to start graceful shutdown server" << std::endl;
    return 1;
  }

  std::cout << "graceful shutdown server listening on " << host << ":" << port << std::endl;
#if defined(CXPNET_PLATFORM_WINDOWS)
  std::cout << "press Ctrl+C/Ctrl+Break once for shutdown; again for close" << std::endl;
#else
  std::cout << "send SIGINT/SIGTERM once to call Server::shutdown(); send it again while connections remain to call Server::close()" << std::endl;
#endif

  std::atomic_bool done {false};
  std::atomic_bool graceful_requested {false};
  std::atomic_bool graceful_done {false};
  std::atomic_bool force_requested {false};
  std::atomic_bool force_done {false};

  std::thread signal_thread([&]() {
#if defined(CXPNET_PLATFORM_WINDOWS)
    LONG handled_requests = 0;
#endif
    while (!done.load(std::memory_order_acquire)) {
#if defined(CXPNET_PLATFORM_WINDOWS)
      if (InterlockedCompareExchange(&stop_requests, 0, 0) <= handled_requests) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      ++handled_requests;
#else
      int signal = 0;
      if (sigwait(&stop_signals, &signal) != 0) {
        continue;
      }
#endif

      if (done.load(std::memory_order_acquire)) {
        return;
      }

      bool expected = false;
      if (graceful_requested.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        std::cout << "graceful shutdown requested" << std::endl;
        server.shutdown();
        graceful_done.store(true, std::memory_order_release);
        continue;
      }

      expected = false;
      if (force_requested.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        std::cout << "force close requested" << std::endl;
        server.close();
        force_done.store(true, std::memory_order_release);
        return;
      }
    }
  });

  while (true) {
    server.poll();

    if ((graceful_done.load(std::memory_order_acquire) || force_done.load(std::memory_order_acquire))
        && server.connection_count() == 0) {
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  done.store(true, std::memory_order_release);
#if !defined(CXPNET_PLATFORM_WINDOWS)
  if (!force_done.load(std::memory_order_acquire)) {
    pthread_kill(signal_thread.native_handle(), SIGTERM);
  }
#endif
  signal_thread.join();
#if defined(CXPNET_PLATFORM_WINDOWS)
  SetConsoleCtrlHandler(handle_console_control, FALSE);
#endif
  return 0;
}
