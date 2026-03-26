#include "cxpnet/cxpnet.h"

#include <atomic>
#include <iostream>
#include <thread>

using namespace cxpnet;

class EchoClient {
public:
  EchoClient(const std::string& addr, uint16_t port, int message_count = 1000)
      : addr_(addr)
      , port_(port)
      , message_count_(message_count) {
  }

  void connect() {
    conn_ = std::make_shared<Conn>(&event_poll_);
    conn_->connect(addr_.c_str(), port_,
        [this](ConnPtr conn) {
          std::cout << "Connected to echo server" << std::endl;
          conn->set_conn_user_callbacks(
              [this](Buffer* buffer) {
                on_message_(buffer);
              },
              [this](int err) {
                on_close_(err);
              });
          send_next_message_();
        },
        [this](int err) {
          std::cout << "Connection error: " << err << std::endl;
          event_poll_.shutdown();
        });
  }

  void run() {
    event_poll_.run();
  }

  void print_stats() const {
    std::cout << "=== Client Statistics ===" << std::endl;
    std::cout << "Sent messages: " << sent_count_ << std::endl;
    std::cout << "Received messages: " << received_count_ << std::endl;
    std::cout << "========================" << std::endl;
  }

private:
  void send_next_message_() {
    if (sent_count_ >= message_count_ || conn_ == nullptr || !conn_->connected()) {
      return;
    }

    std::string msg = "Message" + std::to_string(sent_count_ + 1) + " ";
    conn_->send(msg);
    sent_count_++;
  }

  void on_message_(Buffer* buffer) {
    std::string msg(buffer->peek(), buffer->readable_size());
    std::cout << "Received echo: " << msg << std::endl;
    received_count_++;
    buffer->been_read_all();

    if (received_count_ >= message_count_) {
      std::cout << "All messages received. Stopping client." << std::endl;
      if (conn_ != nullptr) {
        conn_->shutdown();
      }
      return;
    }

    send_next_message_();
  }

  void on_close_(int err) {
    std::cout << "Connection closed with error: " << err << std::endl;
    event_poll_.shutdown();
  }

  std::string      addr_;
  uint16_t         port_;
  int              message_count_;
  std::atomic<int> sent_count_ {0};
  std::atomic<int> received_count_ {0};
  IOEventPoll      event_poll_;
  ConnPtr          conn_;
};

int main() {
  EchoClient client("127.0.0.1", 9092, 1000);

  std::thread t([&client]() {
    client.run();
  });

  client.connect();
  t.join();
  client.print_stats();
  return 0;
}
