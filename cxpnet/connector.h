#ifndef CONNECTOR_H
#define CONNECTOR_H

#include "io_base.h"
#include "io_event_poll.h"

namespace cxpnet {
  class Connector {
  public:
    Connector(IOEventPoll* event_poll, const char* addr, uint16_t port) {
      event_poll_ = event_poll_;
      IPType ip_type = ip_address_type(std::string(addr));
      ProtocolStack proto_stack = ProtocolStack::kIPv4Only;
      if (ip_type == IPType::kIPv6) { proto_stack = ProtocolStack::kIPv6Only; }
      remote_addr_storage_ = platform::get_sockaddr(addr, port, proto_stack);
    }
    ~Connector() {}
    bool connect() {
      if (remote_addr_storage_.ss_family == 0) { return false; }

      handle_ = platform::connect(remote_addr_storage_);
      if (handle_ == -1) { return false; }

      channel_ = std::make_unique<Channel>(event_poll_, handle_);
      channel_->set_read_callback(std::bind(&Connector::_handle_read, this));
      channel_->add_read_event();
    }
  private:
    void _handle_read() {}
  private:
    int                      handle_     = -1;
    IOEventPoll*             event_poll_ = nullptr;
    std::unique_ptr<Channel> channel_    = nullptr;
    struct sockaddr_storage  remote_addr_storage_;
  };
} // namespace cxpnet

#endif // CONNECTOR_H