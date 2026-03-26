#include "cxpnet/cxpnet.h"

#include <iostream>
#include <mutex>
#include <unordered_map>
#include <vector>

using namespace cxpnet;

class ChatServer {
public:
  ChatServer(const std::string& addr, uint16_t port, int thread_num = 4)
      : server_(addr.c_str(), port, ProtocolStack::kIPv4Only, SocketOption::kReuseAddr) {
    server_.set_thread_num(thread_num);
    server_.set_conn_user_callback([this](const ConnPtr& conn) {
      std::cout << "New connection from "
                << conn->remote_addr_and_port().first << ":"
                << conn->remote_addr_and_port().second << std::endl;

      {
        std::lock_guard<std::mutex> lock(mutex_);
        connections_[conn->native_handle()] = conn;
      }

      conn->set_conn_user_callbacks(
          [this, conn](Buffer* buffer) {
            on_message_(conn, buffer);
          },
          [this, conn](int err) {
            on_close_(conn, err);
          });
    });
  }

  int start() {
    if (!server_.start(RunningMode::kOnePollPerThread)) {
      std::cerr << "Failed to start chat server" << std::endl;
      return 1;
    }

    std::cout << "Chat server started, listening on port 9091" << std::endl;
    server_.run();
    return 0;
  }

private:
  void on_message_(const ConnPtr& sender, Buffer* buffer) {
    std::string msg(buffer->peek(), buffer->readable_size());
    const auto [addr, port] = sender->remote_addr_and_port();
    std::string broadcast_msg = std::string(addr) + ":" + std::to_string(port) + " " + msg;
    buffer->been_read_all();

    std::cout << "Message received: " << broadcast_msg << std::endl;
    broadcast_message_(broadcast_msg);
  }

  void on_close_(const ConnPtr& conn, int err) {
    std::cout << "Connection closed with error: " << err << std::endl;

    std::lock_guard<std::mutex> lock(mutex_);
    connections_.erase(conn->native_handle());
  }

  void broadcast_message_(std::string_view msg) {
    std::vector<ConnPtr> targets;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      targets.reserve(connections_.size());
      for (const auto& [handle, conn] : connections_) {
        if (conn != nullptr && conn->connected()) {
          targets.push_back(conn);
        }
      }
    }

    for (const auto& conn : targets) {
      if (conn != nullptr && conn->connected()) {
        conn->send(msg);
      }
    }
  }

  Server                           server_;
  std::unordered_map<int, ConnPtr> connections_;
  std::mutex                       mutex_;
};

int main() {
  ChatServer server("127.0.0.1", 9091, 4);
  return server.start();
}
