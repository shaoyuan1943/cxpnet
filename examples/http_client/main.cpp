#include "cxpnet/cxpnet.h"

#include <chrono>
#include <iostream>
#include <thread>

using namespace cxpnet;

class HttpClient {
public:
  HttpClient(const std::string& addr, uint16_t port)
      : addr_(addr)
      , port_(port) {
  }

  void connect() {
    conn_ = std::make_shared<Conn>(&event_poll_);

    conn_->connect(addr_.c_str(), port_,
        [this](ConnPtr conn) {
          std::cout << "Connected to HTTP server" << std::endl;

          // Set up message and close callbacks
          conn->set_conn_user_callbacks(
              [this](Buffer* buffer) {
                this->onMessage(buffer);
              },
              [this](int err) {
                this->onClose(err);
              });

          // Send HTTP GET request
          std::string request =
              "GET / HTTP/1.1\r\n"
              "Host: " +
              addr_ + "\r\n"
                      "Connection: close\r\n"
                      "\r\n";

          conn->send(request);
        },
        [this](int err) {
          std::cout << "Connection error: " << err << std::endl;
          event_poll_.shutdown();
        });
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
    std::cout << "HTTP Response:" << std::endl;
    std::cout << response << std::endl;
    buffer->been_read_all();

    // Close connection after receiving response
    conn_->shutdown();
  }

  void onClose(int err) {
    std::cout << "Connection closed with error: " << err << std::endl;
    conn_.reset();
    event_poll_.shutdown();
  }

  std::string addr_;
  uint16_t   port_;
  IOEventPoll event_poll_;
  ConnPtr    conn_;
};

int main() {
  HttpClient client("127.0.0.1", 8080);
  std::thread t([&client]() {
    client.run();
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  client.connect();

  t.join();

  return 0;
}
