#include "cxpnet/cxpnet.h"

#include <atomic>
#include <iostream>

using namespace cxpnet;

class EchoServer {
public:
  EchoServer(const std::string& addr, uint16_t port, int thread_num = 4)
      : server_(addr.c_str(), port, ProtocolStack::kIPv4Only, SocketOption::kReuseAddr) {
    server_.set_thread_num(thread_num);
    server_.set_conn_user_callback([this](const ConnPtr& conn) {
      connection_count_++;
      std::cout << "New connection from "
                << conn->remote_addr_and_port().first << ":"
                << conn->remote_addr_and_port().second
                << " (Total connections: " << connection_count_ << ")" << std::endl;

      conn->set_conn_user_callbacks(
          [this, conn](Buffer* buffer) {
            on_message_(conn, buffer);
          },
          [this](int err) {
            on_close_(err);
          });
    });
  }

  int start() {
    if (!server_.start(RunningMode::kOnePollPerThread)) {
      std::cerr << "Failed to start echo server" << std::endl;
      return 1;
    }

    std::cout << "Echo server started, listening on port 9092" << std::endl;
    server_.run();
    return 0;
  }

private:
  void on_message_(const ConnPtr& conn, Buffer* buffer) {
    message_count_++;
    std::string msg(buffer->peek(), buffer->readable_size());
    std::cout << "msg: " << msg << std::endl;
    conn->send(msg);
    buffer->been_read_all();
  }

  void on_close_(int err) {
    connection_count_--;
    std::cout << "Connection closed"
              << " (Total connections: " << connection_count_ << ")"
              << " with error: " << err << std::endl;
  }

  Server                 server_;
  std::atomic<int>       connection_count_ {0};
  std::atomic<long long> message_count_ {0};
};

int main() {
  EchoServer server("127.0.0.1", 9092, 4);
  return server.start();
}
