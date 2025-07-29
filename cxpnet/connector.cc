
#include "connector.h"
#include "channel.h"
#include "ensure.h"
#include "conn.h"
#include "io_event_poll.h"
#include "platform_api.h"
#include <functional>
#include <memory>

namespace cxpnet {
  Connector::Connector(IOEventPoll* event_poll, std::string_view addr, uint16_t port) {
    event_poll_ = event_poll;
    addr_       = std::string(addr);
    port_       = port;
  }
  
  Connector::~Connector() {
    if (channel_) {
      channel_->clear_event();
      channel_->remove();
    }
  }

  void Connector::start() {
    event_poll_->run_in_poll([self = shared_from_this()]() {
      self->_start_in_poll();
    });
  }

  ConnPtr Connector::start_by_sync() {
    IPType        ip_type     = ip_address_type(addr_);
    ProtocolStack proto_stack = (ip_type == IPType::kIPv4) ? ProtocolStack::kIPv4Only : ProtocolStack::kIPv6Only;
    if (ip_type == IPType::kInvalid) { return nullptr; }

    struct sockaddr_storage addr_storage = Platform::get_sockaddr(addr_.c_str(), port_, proto_stack);
    if (addr_storage.ss_family == 0) { return nullptr; }
    // sync connect
    int handle = Platform::connect(addr_storage, false);
    if (handle < 0) {
      if (on_error_func_) { on_error_func_(Platform::get_last_error()); }
      return nullptr;
    }

    _set_state(State::kConnected);
    ConnPtr conn = std::make_shared<Conn>(event_poll_, handle);
    conn->set_remote_addr(addr_.c_str(), port_);
    conn->_start();

    return conn;
  }

  void Connector::_start_in_poll() {
    ENSURE(event_poll_->is_in_poll_thread(), "Must in IO thread");

    IPType        ip_type     = ip_address_type(addr_);
    ProtocolStack proto_stack = (ip_type == IPType::kIPv4) ? ProtocolStack::kIPv4Only : ProtocolStack::kIPv6Only;
    if (ip_type == IPType::kInvalid) { return; }

    struct sockaddr_storage addr_storage = Platform::get_sockaddr(addr_.c_str(), port_, proto_stack);
    if (addr_storage.ss_family == 0) { return; }

    int handle = Platform::connect(addr_storage);
    if (handle < 0) {
      if (on_error_func_) { on_error_func_(Platform::get_last_error()); }
      return;
    }

    // EINPROGRESS or connected immediately
    _set_state(State::kConnecting);
    channel_ = std::make_unique<Channel>(event_poll_, handle);
    channel_->set_write_callback(std::bind(&Connector::_handle_write, shared_from_this()));
    channel_->add_write_event();
  }

  void Connector::_handle_write() {
    if (state_ == State::kConnecting) {
      int       handle = _remove_and_get_handle();
      int       err    = 0;
      socklen_t len    = sizeof(err);
      if (getsockopt(handle, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
        Platform::close_handle(handle);
        if (on_error_func_) { on_error_func_(err); }
      } else {
        _set_state(State::kConnected);
        ConnPtr conn = std::make_shared<Conn>(event_poll_, handle);
        conn->set_remote_addr(addr_.c_str(), port_);
        conn->_start();

        if (on_conn_func_) { on_conn_func_(conn); }
      }
    }
  }

  int Connector::_remove_and_get_handle() {
    int handle = channel_->handle();
    channel_->clear_event();
    channel_->remove();
    channel_.reset(); // The channel has done its job
    return handle;
  }
} // namespace cxpnet