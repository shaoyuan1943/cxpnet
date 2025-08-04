
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
    connector_  = std::make_unique<Connector>(poll, addr, port);
    connector_->set_conn_user_callback(std::bind(&AsyncClient::_on_connection, this, std::placeholders::_1));
    connector_->set_error_user_callback(std::bind(&AsyncClient::_on_connector_error, this, std::placeholders::_1));
  }
  ~AsyncClient() {}

  void connect() { connector_->start(); }
  void disconnect() {
    if (conn_) { conn_->shutdown(); }
  }

  void send(std::string_view msg) { conn_->send(std::move(msg)); }
  void send(std::string&& msg) { conn_->send(std::move(msg)); }

  void _on_conn_message(const ConnPtr& conn, Buffer* buff) {
    if (buff->readable_size() > 0) {
      std::string msg(buff->peek(), buff->readable_size());
      buff->been_read_all();

      std::cout << "recv msg: " << msg << std::endl;
    }
  }

  void _on_conn_close(const ConnPtr& conn, int err) {
    std::cout << conn->native_handle() << " closed, reason err: " << err << std::endl;
    // reconnect
    connect();
  }
private:
  void _on_connection(const ConnPtr& conn) {
    LOG_DEBUG("New connection....");
    conn_ = conn;

    conn_->set_conn_user_callbacks(
        std::bind(&AsyncClient::_on_conn_message, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&AsyncClient::_on_conn_close, this, std::placeholders::_1, std::placeholders::_2));
    
    conn_->send("Hello, I'm client.");
  }
  // connect failed
  void _on_connector_error(int err) {
    std::cout << "connector closed err: " << err << std::endl;
    // reconnect
    connect();
  }
private:
  IOEventPoll*               event_poll_ = nullptr;
  std::string                addr_       = "";
  uint16_t                   port_       = 0;
  std::shared_ptr<Connector> connector_;
  ConnPtr                    conn_;
  std::mutex                 mutex_;
};

int main() {
  using namespace cxpnet;
  IOEventPoll event_poll;
  AsyncClient client(&event_poll, "127.0.0.1", 9090);
  client.connect();
  event_poll.run();   
}