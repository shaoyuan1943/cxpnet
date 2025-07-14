#ifndef ACCEPTOR_H
#define ACCEPTOR_H

#include "io_base.h"
#include "io_event_poll.h"

namespace cxpnet {
  class Acceptor {
  public:
    Acceptor(IOEventPoll* event_poll, const char* addr, uint16_t port,
             ProtocolStack proto_stack = ProtocolStack::kIPv4Only, int option = SocketOption::kNone) {
      event_poll_         = event_poll;
      local_addr_storage_ = platform::get_sockaddr(addr, port, proto_stack);
      sock_option_        = option;
      proto_stack_        = proto_stack;
      listening_          = false;
    }
    ~Acceptor() {
      platform::close_handle(listen_handle_);
    }

    void shutdown() {
      if (channel_) {
        channel_->clear_event();
        channel_->remove();
      }
    }

    bool listen() {
      if (local_addr_storage_.ss_family == 0) { return false; }
      listen_handle_ = platform::listen(local_addr_storage_, proto_stack_, sock_option_);
      if (listen_handle_ == -1) { return false; }

      listening_ = true;
      channel_   = std::make_unique<Channel>(event_poll_, listen_handle_);
      channel_->set_read_callback(std::bind(&Acceptor::_handle_read, this));
      channel_->add_read_event();

      return listening_;
    }

    bool is_listen() { return listening_; }

    void set_connection_callback(std::function<void(int, struct sockaddr_storage)> conn_func) { on_conn_func_ = std::move(conn_func); }
    void set_acceptor_err_callback(std::function<void(int)> err_func) { on_err_func_ = std::move(err_func); }
  private:
    void _handle_read() {
      int err = platform::accept(listen_handle_, accepted_handles_);
      if (err != 0) {
        if (on_err_func_ != nullptr) { on_err_func_(err); }
        return;
      }

      for (auto&& p : accepted_handles_) {
        if (on_conn_func_ != nullptr) { on_conn_func_(p.first, p.second); }
      }

      accepted_handles_.clear();
    }
  private:
    using HandlesListType                    = std::vector<std::pair<int, struct sockaddr_storage>>;
    using NewConnectionCallbackType          = std::function<void(int, struct sockaddr_storage)>;
    int                       listen_handle_ = -1;
    IOEventPoll*              event_poll_    = nullptr;
    std::unique_ptr<Channel>  channel_       = nullptr;
    bool                      listening_     = false;
    int                       sock_option_   = SocketOption::kNone;
    ProtocolStack             proto_stack_   = ProtocolStack::kIPv4Only;
    struct sockaddr_storage   local_addr_storage_;
    HandlesListType           accepted_handles_;
    std::function<void(int)>  on_err_func_  = nullptr;
    NewConnectionCallbackType on_conn_func_ = nullptr;
  };
} // namespace cxpnet

#endif // ACCEPTOR_H