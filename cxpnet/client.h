#ifndef CLIENT_H
#define CLIENT_H

#include "io_base.h"
#include "conn.h"
#include "io_event_poll.h"

namespace cxpnet {
  class Client {
  public:
    Client(const char* addr, uint16_t port) {
      IPType        ip_type     = ip_address_type(std::string(addr));
      ProtocolStack proto_stack = ProtocolStack::kIPv4Only;
      if (ip_type == IPType::kIPv6) { proto_stack = ProtocolStack::kIPv6Only; }
      remote_addr_storage_ = platform::get_sockaddr(addr, port, proto_stack);
      event_poll_          = std::make_unique<IOEventPoll>();
    }
    ~Client() {}

    bool connect() {
      if (remote_addr_storage_.ss_family == 0) { return false; }

      handle_ = platform::connect(remote_addr_storage_);
      if (handle_ == invalid_socket) { return; }

      conn_ = std::make_shared<Conn>(event_poll_.get(), handle_);
      return true;
    }
    bool is_connected() {
      if (conn_.get() != nullptr && conn_->connected()) { return true; }
      return false;
    }
  protected:
    std::shared_ptr<Conn> conn_;
  private:
    int                          handle_ = -1;
    std::unique_ptr<IOEventPoll> event_poll_;
    struct sockaddr_storage      remote_addr_storage_;
    std::unique_ptr<std::thread> conn_thread_;
  };
} // namespace cxpnet

#endif // CLIENT_H