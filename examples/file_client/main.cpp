#include "cxpnet/cxpnet.h"

#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

using namespace cxpnet;

class FileClient {
public:
  FileClient(const std::string& addr, uint16_t port)
      : addr_(addr)
      , port_(port) {
  }

  void connect() {
    conn_ = std::make_shared<Conn>(&event_poll_);

    conn_->connect(addr_.c_str(), port_,
        [this](ConnPtr conn) {
          std::cout << "Connected to file server" << std::endl;

          conn->set_conn_user_callbacks(
              [this](Buffer* buffer) {
                this->onMessage(buffer);
              },
              [this](int err) {
                this->onClose(err);
              });

          std::lock_guard<std::mutex> lock(mutex_);
          if (!pending_request_.empty()) {
            conn_->send(pending_request_);
            pending_request_.clear();
          }
        },
        [this](int err) {
          std::cout << "Connection error: " << err << std::endl;
          event_poll_.shutdown();
        });
  }

  void requestFile(const std::string& filename) {
    std::string request = "GET " + filename;

    std::lock_guard<std::mutex> lock(mutex_);
    if (conn_ && conn_->connected()) {
      conn_->send(request);
      return;
    }

    pending_request_ = std::move(request);
  }

  void disconnect() {
    if (conn_) {
      conn_->shutdown();
    }
  }

  void run() {
    event_poll_.run();
  }

private:
  void onMessage(Buffer* buffer) {
    std::string response(buffer->peek(), buffer->readable_size());
    std::cout << "File server response:" << std::endl;
    std::cout << response << std::endl;
    buffer->been_read_all();
    disconnect();
  }

  void onClose(int err) {
    std::cout << "File transfer connection closed with error: " << err << std::endl;
    conn_.reset();
    event_poll_.shutdown();
  }

  std::string addr_;
  uint16_t    port_;
  IOEventPoll event_poll_;
  ConnPtr     conn_;
  std::mutex  mutex_;
  std::string pending_request_;
};

int main() {
  FileClient client("127.0.0.1", 9094);
  std::thread t([&client]() {
    client.run();
  });

  std::string filename;
  std::cout << "Enter filename to request: ";
  std::getline(std::cin, filename);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  client.connect();
  client.requestFile(filename);

  t.join();
  return 0;
}
