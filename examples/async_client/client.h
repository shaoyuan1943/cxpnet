#ifndef CLIENT_H
#define CLIENT_H

#include "buffer.h"
#include "conn.h"
#include "connector.h"
#include "io_event_poll.h"
#include <iostream>
#include <mutex>
#include <string>

using namespace cxpnet;
class Client : public NonCopyable {
public:
  Client(IOEventPoll* poll, const char* addr, uint16_t port) {
    event_poll_ = poll;
    addr_       = std::string(addr);
    port_       = port;
    connector_  = std::make_unique<Connector>(poll, addr, port);
    connector_->set_conn_user_callback(std::bind(&Client::_on_connection, this, std::placeholders::_1));
    connector_->set_error_user_callback(std::bind(&Client::_on_connector_error, this, std::placeholders::_1));
  }

  ~Client() {}

  void connect() { connector_->start(); }
  void disconnect() {
    if (conn_) { conn_->shutdown(); }
  }

  void send(const std::string& msg, std::function<void(bool)> func = nullptr) {
    conn_->send(msg.data(), msg.size(), func);
  }
  void send(const std::string_view msg, std::function<void(bool)> func = nullptr) {
    conn_->send(msg.data(), msg.size(), func);
  }
  void send(const char* msg, size_t size, std::function<void(bool)> func = nullptr) {
    conn_->send(msg, size, func);
  }
private:
  void _on_conn_message(const ConnPtr& conn, const char* data, size_t size) {
    conn_->send(data, size);
  }

  void _on_conn_close(const ConnPtr& conn, int err) {
    std::cout << conn->native_handle() << " closed, reason err: " << err << std::endl;

    // reconnect
    connect();
  }
private:
  // connect success
  void _on_connection(const ConnPtr& conn) {
    conn_ = conn;

    conn->set_conn_user_callbacks(
        std::bind(&Client::_on_conn_message, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
        std::bind(&Client::_on_conn_close, this, std::placeholders::_1, std::placeholders::_2));
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


#endif // CLIENT_H