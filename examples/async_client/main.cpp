
#include "cxpnet/cxpnet.h"

#include <iostream>
#include <mutex>
#include <string>

using namespace cxpnet;

class AsyncClient : public cxpnet::NonCopyable {
public:
  AsyncClient(IOEventPoll* poll, const char* addr, uint16_t port) {
    event_poll_ = poll;
    addr_       = std::string(addr);
    port_       = port;
  }
  ~AsyncClient() {}

  void connect() {
    conn_ = std::make_shared<Conn>(event_poll_);
    conn_->connect(addr_.c_str(), port_,
        std::bind(&AsyncClient::on_connection_, this, std::placeholders::_1),
        std::bind(&AsyncClient::on_connector_error_, this, std::placeholders::_1));
  }

  void disconnect() {
    if (conn_) { conn_->shutdown(); }
  }

  void send(std::string_view msg) { conn_->send(msg); }

  void on_conn_message_(Buffer* buff) {
    if (buff->readable_size() > 0) {
      std::string msg(buff->peek(), buff->readable_size());
      buff->been_read_all();

      std::cout << "recv msg: " << msg << std::endl;
    }
  }

  void on_conn_close_(int err) {
    std::cout << "Connection closed, reason err: " << err << std::endl;
    // reconnect
    connect();
  }

private:
  void on_connection_(const ConnPtr& conn) {
    LOG_DEBUG("New connection....");
    conn_ = conn;

    conn_->set_conn_user_callbacks(
        std::bind(&AsyncClient::on_conn_message_, this, std::placeholders::_1),
        std::bind(&AsyncClient::on_conn_close_, this, std::placeholders::_1));

    conn_->send("Hello, I'm client.");
  }

  // connect failed
  void on_connector_error_(int err) {
    std::cout << "connector closed err: " << err << std::endl;
    // reconnect
    connect();
  }

private:
  IOEventPoll* event_poll_ = nullptr;
  std::string  addr_       = "";
  uint16_t     port_       = 0;
  ConnPtr      conn_;
  std::mutex   mutex_;
};

int main() {
  using namespace cxpnet;
  IOEventPoll event_poll;
  AsyncClient client(&event_poll, "127.0.0.1", 9090);
  client.connect();
  event_poll.run();
}
