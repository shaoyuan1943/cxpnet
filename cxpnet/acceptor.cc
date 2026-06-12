#include "acceptor.h"
#include "channel.h"
#include "io_event_poll.h"
#include "platform_api.h"

#include <future>

namespace cxpnet {
  Acceptor::Acceptor(IOEventPoll* event_poll)
      : event_poll_ {event_poll} {
  }

  Acceptor::~Acceptor() { close(); }

  void Acceptor::set_listen_addr(const char* addr, uint16_t port,
                                 ProtocolStack proto_stack, int option) {
    sock_option_        = option;
    proto_stack_        = proto_stack;
    local_addr_storage_ = Platform::get_sockaddr(addr, port, proto_stack);
  }

  void Acceptor::close() {
    State old_state = state_.exchange(State::kClosed, std::memory_order_acq_rel);
    if (old_state != State::kListening) { return; }

    if (event_poll_ == nullptr) {
      close_local_();
      return;
    }

    if (event_poll_->is_in_poll_thread()) {
      close_in_poll_();
      return;
    }

    if (event_poll_->is_shutdown()) {
      close_local_();
      return;
    }

    // Acceptor is expected to be closed before its owner poll stops.
    auto done   = std::make_shared<std::promise<void>>();
    auto future = done->get_future();

    event_poll_->run_in_poll([this, done]() {
      close_in_poll_();
      done->set_value();
    });

    future.get();
  }

  void Acceptor::close_local_() {
    channel_.reset();

    if (listen_handle_ != invalid_socket) {
      Platform::close_handle(listen_handle_);
      listen_handle_ = invalid_socket;
    }
  }

  void Acceptor::close_in_poll_() {
    if (channel_) {
      channel_->unregister();
      auto old_channel = std::shared_ptr<Channel>(channel_.release());
      event_poll_->run_later([old_channel]() {});
    }

    if (listen_handle_ != invalid_socket) {
      Platform::close_handle(listen_handle_);
      listen_handle_ = invalid_socket;
    }
  }

  bool Acceptor::listen() {
    if (local_addr_storage_.ss_family == 0) { return false; }

    listen_handle_ = Platform::listen(local_addr_storage_, proto_stack_, sock_option_);
    if (listen_handle_ == invalid_socket) { return false; }

    channel_ = std::make_unique<Channel>(event_poll_, listen_handle_);
    channel_->set_read_callback(std::bind(&Acceptor::handle_read_, this));
    channel_->add_read_event();

    set_state_(State::kListening);
    return true;
  }

  void Acceptor::handle_read_() {
    if (get_state_() != State::kListening || listen_handle_ == invalid_socket) {
      return;
    }

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
