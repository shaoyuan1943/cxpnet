#ifndef CONNECTOR_H
#define CONNECTOR_H

#include "base_type_value.h"
#include "channel.h"
#include "client.h"
#include "conn.h"
#include "ensure.h"
#include "io_event_poll.h"
#include "platform_api.h"
#include <functional>
#include <memory>

namespace cxpnet {
  class Connector : public NonCopyable
      , public std::enable_shared_from_this<Connector> {
  public:
    Connector(IOEventPoll* event_poll, const char* addr, uint16_t port) {
      event_poll_ = event_poll;
      addr_       = std::string(addr);
      port_       = port;
    }

    ~Connector() {
      if (channel_) {
        channel_->clear_event();
        channel_->remove();
      }
    }

    void start() {
      event_poll_->run_in_poll([self = shared_from_this()]() {
        self->_start_in_poll();
      });
    }
    void set_conn_user_callback(std::function<void(ConnPtr)> func) { on_conn_func_ = std::move(func); }
    void set_error_user_callback(std::function<void(int)> func) { on_error_func_ = std::move(func); }
  private:
    void _set_state(State s) { state_ = s; }
    void _start_in_poll() {
      ENSURE(event_poll_->is_in_poll_thread(), "Must in IO thread");

      IPType        ip_type     = ip_address_type(addr_);
      ProtocolStack proto_stack = (ip_type == IPType::kIPv4) ? ProtocolStack::kIPv4Only : ProtocolStack::kIPv6Only;
      if (ip_type == IPType::kInvalid) { return; }

      struct sockaddr_storage addr_storage = platform::get_sockaddr(addr_.c_str(), port_, proto_stack);
      if (addr_storage.ss_family == 0) { return; }

      int handle = platform::connect(addr_storage);
      if (handle < 0) {
        if (on_error_func_) { on_error_func_(platform::get_last_error()); }
        return;
      }

      // EINPROGRESS or connected immediately
      _set_state(State::kConnecting);
      channel_ = std::make_unique<Channel>(event_poll_, handle);
      channel_->set_write_callback(std::bind(&Connector::_handle_write, shared_from_this()));
      channel_->add_write_event();
    }
    void _handle_write() {
      if (state_ == State::kConnecting) {
        int       handle = _remove_and_get_handle();
        int       err    = 0;
        socklen_t len    = sizeof(err);
        if (getsockopt(handle, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
          platform::close_handle(handle);
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
    int _remove_and_get_handle() {
      int handle = channel_->handle();
      channel_->clear_event();
      channel_->remove();
      channel_.reset(); // The channel has done its job
      return handle;
    }
  private:
    IOEventPoll*                 event_poll_ = nullptr;
    std::string                  addr_       = "";
    uint16_t                     port_       = 0;
    State                        state_      = State::kDisconnected;
    bool                         started_    = false;
    std::unique_ptr<Channel>     channel_;
    std::function<void(ConnPtr)> on_conn_func_  = nullptr;
    std::function<void(int)>     on_error_func_    = nullptr;
  };
} // namespace cxpnet

#endif // CONNECTOR_H