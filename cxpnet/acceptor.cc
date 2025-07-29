#include "acceptor.h"
#include "channel.h"
#include "platform_api.h"

namespace cxpnet {
  Acceptor::Acceptor(IOEventPoll* event_poll, const char* addr, uint16_t port,
                     ProtocolStack proto_stack, int option) {
    event_poll_         = event_poll;
    local_addr_storage_ = Platform::get_sockaddr(addr, port, proto_stack);
    sock_option_        = option;
    proto_stack_        = proto_stack;
    listening_          = false;
  }

  Acceptor::~Acceptor() { Platform::close_handle(listen_handle_); }

  void Acceptor::shutdown() {
    if (channel_) {
      channel_->clear_event();
      channel_->remove();
    }
    listening_ = false;
  }
  bool Acceptor::listen() {
    if (local_addr_storage_.ss_family == 0) { return false; }
    listen_handle_ = Platform::listen(local_addr_storage_, proto_stack_, sock_option_);
    if (listen_handle_ == -1) { return false; }

    listening_ = true;
    channel_   = std::make_unique<Channel>(event_poll_, listen_handle_);
    channel_->set_read_callback(std::bind(&Acceptor::_handle_read, this));
    channel_->add_read_event();

    return listening_;
  }

  void Acceptor::_handle_read() {
    int err = Platform::accept(listen_handle_, accepted_handles_);
    if (err != 0) {
      if (on_err_func_ != nullptr) { on_err_func_(err); }
      return;
    }

    for (auto&& p : accepted_handles_) {
      if (on_conn_func_ != nullptr) { on_conn_func_(p.first, p.second); }
    }

    accepted_handles_.clear();
  }
} // namespace cxpnet
