#include "cxpnet/cxpnet.h"

#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace cxpnet;

class TcpClient {
public:
  TcpClient(const std::string& addr, uint16_t port)
      : addr_(addr)
      , port_(port) {
  }

  void connect() {
    conn_ = std::make_shared<Conn>(&event_poll_);
    conn_->connect(addr_.c_str(), port_,
        [this](ConnPtr conn) {
          std::cout << "Connected to server" << std::endl;
          conn->set_conn_user_callbacks(
              [this](Buffer* buffer) {
                on_message_(buffer);
              },
              [this](int err) {
                on_close_(err);
              });
          flush_pending_messages_();
        },
        [this](int err) {
          std::cout << "Connection error: " << err << std::endl;
          event_poll_.shutdown();
        });
  }

  void disconnect() {
    if (conn_ != nullptr) {
      conn_->shutdown();
    }
  }

  void send(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (conn_ != nullptr && conn_->connected()) {
      conn_->send(msg);
      return;
    }

    pending_messages_.push_back(msg);
  }

  void run() {
    event_poll_.run();
  }

private:
  void flush_pending_messages_() {
    std::vector<std::string> pending_messages;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_messages.swap(pending_messages_);
    }

    for (const auto& msg : pending_messages) {
      if (conn_ != nullptr && conn_->connected()) {
        conn_->send(msg);
      }
    }
  }

  void on_message_(Buffer* buffer) {
    std::string msg(buffer->peek(), buffer->readable_size());
    std::cout << "Received: " << msg << std::endl;
    buffer->been_read_all();
  }

  void on_close_(int err) {
    std::cout << "Connection closed with error: " << err << std::endl;
    event_poll_.shutdown();
  }

  std::string              addr_;
  uint16_t                 port_;
  IOEventPoll              event_poll_;
  ConnPtr                  conn_;
  std::mutex               mutex_;
  std::vector<std::string> pending_messages_;
};

int main() {
  TcpClient client("127.0.0.1", 9090);

  std::thread t([&client]() {
    client.run();
  });

  client.connect();

  std::string line;
  while (std::getline(std::cin, line)) {
    if (line == "quit") {
      client.disconnect();
      break;
    }
    client.send(line);
  }

  t.join();
  return 0;
}
