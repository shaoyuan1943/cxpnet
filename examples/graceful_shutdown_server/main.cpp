#include <cxpnet/cxpnet.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <pthread.h>
#include <string_view>
#include <thread>

namespace {
  sigset_t make_stop_signal_set() {
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    return signals;
  }
} // namespace

int main(int argc, char* argv[]) {
  const char* host = argc > 1 ? argv[1] : "127.0.0.1";
  uint16_t    port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : 9096;

  sigset_t stop_signals = make_stop_signal_set();
  if (pthread_sigmask(SIG_BLOCK, &stop_signals, nullptr) != 0) {
    std::cerr << "failed to block stop signals" << std::endl;
    return 1;
  }

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
  std::cout << "send SIGINT/SIGTERM once to call Server::shutdown(); send it again while connections remain to call Server::close()" << std::endl;

  std::atomic_bool done {false};
  std::atomic_bool graceful_requested {false};
  std::atomic_bool graceful_done {false};
  std::atomic_bool force_requested {false};
  std::atomic_bool force_done {false};

  std::thread signal_thread([&]() {
    while (!done.load(std::memory_order_acquire)) {
      int signal = 0;
      if (sigwait(&stop_signals, &signal) != 0) {
        continue;
      }

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
  if (!force_done.load(std::memory_order_acquire)) {
    pthread_kill(signal_thread.native_handle(), SIGTERM);
  }
  signal_thread.join();
  return 0;
}
