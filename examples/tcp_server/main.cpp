#include "cxpnet/cxpnet.h"

#include <iostream>

using namespace cxpnet;

class TcpServer {
public:
  TcpServer(const std::string& addr, uint16_t port, int thread_num = 4)
      : server_(addr.c_str(), port, ProtocolStack::kIPv4Only, SocketOption::kReuseAddr) {
    server_.set_thread_num(thread_num);
    server_.set_conn_user_callback([this](const ConnPtr& conn) {
      std::cout << "New connection from "
                << conn->remote_addr_and_port().first << ":"
                << conn->remote_addr_and_port().second << std::endl;

      conn->set_conn_user_callbacks(
          [this, conn](Buffer* buffer) {
            on_message_(conn, buffer);
          },
          [this](int err) {
            on_close_(err);
          });

      conn->send("hello, I'm server");
    });
  }

  int start() {
    if (!server_.start(RunningMode::kOnePollPerThread)) {
      std::cerr << "Failed to start tcp server" << std::endl;
      return 1;
    }

    std::cout << "Server started, listening on port 9090" << std::endl;
    server_.run();
    return 0;
  }

private:
  void on_message_(const ConnPtr& conn, Buffer* buffer) {
    std::string msg(buffer->peek(), buffer->readable_size());
    std::cout << "Received: " << msg << std::endl;
    conn->send(msg);
    buffer->been_read_all();
  }

  void on_close_(int err) {
    std::cout << "Connection closed with error: " << err << std::endl;
  }

  Server server_;
};

int main() {
  TcpServer server("127.0.0.1", 9090, 4);
  return server.start();
}
