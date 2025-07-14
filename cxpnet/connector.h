#ifndef CONNECTOR_H
#define CONNECTOR_H

#include "io_base.h"
#include "platform_api.h"
#include "conn.h"
#include "io_event_poll.h"

namespace cxpnet {
  using ConnPtr = std::shared_ptr<Conn>;
  class Connector {
  public:
    static ConnPtr connect(IOEventPoll* event_poll, const char* addr, uint16_t port) {
      IPType        ip_tye      = ip_address_type(addr);
      ProtocolStack proto_stack = ProtocolStack::kIPv4Only;
      if (ip_tye == IPType::kIPv6) { proto_stack = ProtocolStack::kIPv6Only; }
      struct sockaddr_storage addr_storage = platform::get_sockaddr(addr, port, proto_stack);
      if (addr_storage.ss_family = 0) { return nullptr; }

      int handle = platform::connect(addr_storage);
      if (handle == invalid_socket) { return nullptr; }

      ConnPtr conn = std::make_shared<Conn>(event_poll, handle);
      conn->set_remote_addr(addr, port);
      return conn;
    }
  };
} // namespace cxpnet

#endif // CONNECTOR_H