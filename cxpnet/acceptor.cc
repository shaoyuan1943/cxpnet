#include "acceptor.h"
#include "channel.h"
#include "io_event_poll.h"
#include "platform_api.h"

#include <future>

namespace cxpnet {
  Acceptor::Acceptor(IOEventPoll* event_poll)
      : event_poll_ {event_poll}
      , listen_handle_ {invalid_socket}
      , channel_ {nullptr}
      , listening_ {false}
      , sock_option_ {SocketOption::kNone}
      , proto_stack_ {ProtocolStack::kIPv4Only}
      , on_err_func_ {nullptr}
      , on_conn_func_ {nullptr}
      , local_addr_storage_ {} {
  }

  Acceptor::~Acceptor() { shutdown(); }

  void Acceptor::set_listen_addr(const char* addr, uint16_t port,
                                 ProtocolStack proto_stack, int option) {
    sock_option_        = option;
    proto_stack_        = proto_stack;
    local_addr_storage_ = Platform::get_sockaddr(addr, port, proto_stack);
  }

  void Acceptor::shutdown() {
    if (event_poll_ == nullptr) {
      shutdown_local_();
      return;
    }

    if (event_poll_->is_in_poll_thread()) {
      shutdown_in_poll_();
      return;
    }

    if (event_poll_->is_shutdown()) {
      shutdown_local_();
      return;
    }

    // Acceptor is expected to be shut down before its owner poll stops.
    auto done   = std::make_shared<std::promise<void>>();
    auto future = done->get_future();

    event_poll_->run_in_poll([this, done]() {
      shutdown_in_poll_();
      done->set_value();
    });

    future.get();
  }

  void Acceptor::shutdown_local_() {
    channel_.reset();

    if (listen_handle_ != invalid_socket) {
      Platform::close_handle(listen_handle_);
      listen_handle_ = invalid_socket;
    }

    listening_ = false;
  }

  void Acceptor::shutdown_in_poll_() {
    if (channel_) {
      channel_->clear_event();
      channel_->remove();
      channel_.reset();
    }

    if (listen_handle_ != invalid_socket) {
      Platform::close_handle(listen_handle_);
      listen_handle_ = invalid_socket;
    }

    listening_ = false;
  }

  bool Acceptor::listen() {
    if (local_addr_storage_.ss_family == 0) { return false; }

    listen_handle_ = Platform::listen(local_addr_storage_, proto_stack_, sock_option_);
    if (listen_handle_ == invalid_socket) { return false; }

    listening_ = true;
    channel_   = std::make_unique<Channel>(event_poll_, listen_handle_);
    channel_->set_read_callback(std::bind(&Acceptor::handle_read_, this));
    channel_->add_read_event();

    return true;
  }

  void Acceptor::handle_read_() {
    int err = Platform::accept(listen_handle_, accepted_handles_);
    if (err != 0) {
      if (on_err_func_ != nullptr) { on_err_func_(err); }
      return;
    }

    for (auto&& [handle, addr] : accepted_handles_) {
      if (on_conn_func_ != nullptr) { on_conn_func_(handle, addr); }
    }

    accepted_handles_.clear();
  }
} // namespace cxpnet
