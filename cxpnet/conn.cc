#include "conn.h"
#include "channel.h"
#include "check.h"
#include "io_event_poll.h"
#include "platform_api.h"
#include "timer.h"

#include <atomic>
#include <memory>

namespace cxpnet {
  Conn::Conn(IOEventPoll* event_poll, int handle)
      : event_poll_ {event_poll}
      , handle_ {handle}
      , channel_ {nullptr}
      , on_message_func_ {nullptr}
      , on_close_func_ {nullptr}
      , internal_close_callback_ {nullptr}
      , watermark_func_ {nullptr}
      , high_watermark_ {1024 * 1024}
      , low_watermark_ {256 * 1024}
      , high_watermark_warning_ {false}
      , addr_ {0}
      , port_ {0}
      , state_ {static_cast<int>(State::kDisconnected)}
      , read_buffer_ {nullptr}
      , write_buffer_ {nullptr}
      , on_connected_func_ {nullptr}
      , on_connect_error_func_ {nullptr} {
  }

  Conn::~Conn() {
    cancel_graceful_close_timeout_();
    if (handle_ != invalid_socket) {
      Platform::close_handle(handle_);
    }
  }

  void Conn::connect(const char* addr, uint16_t port,
                     std::function<void(ConnPtr)> on_connected,
                     std::function<void(int)>     on_connect_error) {
    if (get_state_() != State::kDisconnected) {
      if (on_connect_error) { on_connect_error(EALREADY); }
      return;
    }

    RELEASE_STORE(cleanup_done_, false);

    on_connected_func_     = std::move(on_connected);
    on_connect_error_func_ = std::move(on_connect_error);

    strncpy(addr_, addr, INET6_ADDRSTRLEN - 1);
    addr_[INET6_ADDRSTRLEN - 1] = '\0';
    port_                       = port;

    if (event_poll_->is_in_poll_thread()) {
      start_connect_in_poll_(addr, port);
      return;
    }

    auto self = shared_from_this();
    event_poll_->run_in_poll([self, addr_str = std::string(addr), port]() {
      self->start_connect_in_poll_(addr_str.c_str(), port);
    });
  }

  bool Conn::connect_sync(const char* addr, uint16_t port) {
    if (get_state_() != State::kDisconnected) {
      return false;
    }

    RELEASE_STORE(cleanup_done_, false);

    strncpy(addr_, addr, INET6_ADDRSTRLEN - 1);
    addr_[INET6_ADDRSTRLEN - 1] = '\0';
    port_                       = port;

    IPType        ip_type     = ip_address_type(std::string(addr));
    ProtocolStack proto_stack = (ip_type == IPType::kIPv4) ? ProtocolStack::kIPv4Only : ProtocolStack::kIPv6Only;
    if (ip_type == IPType::kInvalid) { return false; }

    struct sockaddr_storage addr_storage = Platform::get_sockaddr(addr, port, proto_stack);
    if (addr_storage.ss_family == 0) { return false; }

    int handle = Platform::connect(addr_storage, false);
    if (handle < 0) { return false; }

    handle_ = handle;
    start_();
    return connected();
  }

  void Conn::start_connect_in_poll_(const char* addr, uint16_t port) {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");

    IPType        ip_type     = ip_address_type(std::string(addr));
    ProtocolStack proto_stack = (ip_type == IPType::kIPv4) ? ProtocolStack::kIPv4Only : ProtocolStack::kIPv6Only;
    if (ip_type == IPType::kInvalid) {
      if (on_connect_error_func_) { on_connect_error_func_(EINVAL); }
      return;
    }

    struct sockaddr_storage addr_storage = Platform::get_sockaddr(addr, port, proto_stack);
    if (addr_storage.ss_family == 0) {
      if (on_connect_error_func_) { on_connect_error_func_(EINVAL); }
      return;
    }

    int handle = Platform::connect(addr_storage);
    if (handle < 0) {
      if (on_connect_error_func_) { on_connect_error_func_(Platform::get_last_error()); }
      return;
    }

    handle_  = handle;
    channel_ = std::make_unique<Channel>(event_poll_, handle_);
    channel_->set_write_callback([this]() { handle_connect_event_(); });
    channel_->set_close_callback([this](int) { handle_connect_event_(); });
    channel_->tie(shared_from_this());
    channel_->add_write_event();

    set_state_(State::kConnecting);
  }

  void Conn::retire_channel_() {
    if (!channel_) { return; }

    auto old_channel = std::shared_ptr<Channel>(channel_.release());
    old_channel->unregister();
    event_poll_->run_later([old_channel]() {});
  }

  void Conn::handle_connect_event_() {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");

    if (get_state_() != State::kConnecting) { return; }

    int       err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(handle_, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
      err = Platform::get_last_error();
    }

    if (err != 0) {
      retire_channel_();

      if (handle_ != invalid_socket) {
        Platform::close_handle(handle_);
        handle_ = invalid_socket;
      }

      set_state_(State::kDisconnected);
      if (on_connect_error_func_) { on_connect_error_func_(err); }
      return;
    }

    retire_channel_();
    start_();

    if (on_connected_func_) { on_connected_func_(shared_from_this()); }
  }

  void Conn::shutdown() {
    if (!connected()) { return; }
    if (handle_ == invalid_socket) { return; }

    if (event_poll_->is_in_poll_thread()) {
      shutdown_in_poll_();
      return;
    }

    auto self = shared_from_this();
    event_poll_->run_in_poll([self]() { self->shutdown_in_poll_(); });
  }

  void Conn::shutdown_in_poll_() {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");

    if (!connected()) { return; }
    if (handle_ == invalid_socket) { return; }

    set_state_(State::kDisconnecting);

    if (!write_buffer_ || write_buffer_->readable_size() == 0) {
      if (channel_) { channel_->remove_write_event(); }
      Platform::shut_wr(handle_);
    }

    if (graceful_close_timeout_ms_ > 0 && event_poll_ && event_poll_->timer_manager()) {
      start_graceful_close_timeout_();
    }
  }

  void Conn::close() {
    if (connected()) { return; }

    if (event_poll_->is_in_poll_thread()) {
      cleanup_(0);
      return;
    }

    auto self = shared_from_this();
    event_poll_->run_in_poll([self]() {
      self->cleanup_(0);
    });
  }

  void Conn::cleanup_(int err) {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");

    bool expected = false;
    if (!cleanup_done_.compare_exchange_strong(expected, true)) { return; }

    cancel_graceful_close_timeout_();

    if (get_state_() == State::kConnected && handle_ != invalid_socket) {
      Platform::shut_wr(handle_);
    }

    set_state_(State::kDisconnecting);
    do_cleanup_(err);
  }

  void Conn::do_cleanup_(int err) {
    if (channel_) { channel_->unregister(); }

    if (handle_ != invalid_socket) {
      Platform::close_handle(handle_);
      handle_ = invalid_socket;
    }

    auto internal_close_callback = std::move(internal_close_callback_);
    auto close_func              = std::move(on_close_func_);

    if (internal_close_callback) { internal_close_callback(); }
    if (close_func) { close_func(err); }

    if (channel_) {
      Channel* raw_channel    = channel_.release();
      auto     channel_shared = std::shared_ptr<Channel>(raw_channel);
      event_poll_->run_later([channel_shared]() {});
    }

    set_state_(State::kDisconnected);
  }

  void Conn::start_graceful_close_timeout_() {
    if (close_timer_id_ != 0) { return; }

    std::weak_ptr<Conn> weak_self = shared_from_this();
    close_timer_id_               = event_poll_->timer_manager()->add_timer(
        graceful_close_timeout_ms_,
        [weak_self]() {
          if (auto self = weak_self.lock()) {
            self->event_poll_->run_in_poll([self]() {
              self->cleanup_(ETIMEDOUT);
            });
          }
        });
  }

  void Conn::cancel_graceful_close_timeout_() {
    if (close_timer_id_ != 0) {
      if (event_poll_ && event_poll_->timer_manager()) {
        event_poll_->timer_manager()->cancel_timer(close_timer_id_);
      }

      close_timer_id_ = 0;
    }
  }

  void Conn::send(const char* msg, size_t size) {
    if (!connected() || msg == nullptr || size == 0) { return; }

    if (event_poll_->is_in_poll_thread()) {
      send_in_poll_thread_(msg, size);
      return;
    }

    auto to_send = std::make_shared<std::string>(msg, size);
    event_poll_->run_in_poll([self = shared_from_this(), to_send]() {
      self->send_in_poll_thread_(to_send->data(), to_send->size());
    });
  }

  void Conn::send(std::string_view msg) {
    send(msg.data(), msg.size());
  }

  std::string Conn::state_string() {
    switch (get_state_()) {
    case State::kDisconnected:
      return "Disconnected";
    case State::kConnecting:
      return "Connecting";
    case State::kConnected:
      return "Connected";
    case State::kDisconnecting:
      return "Disconnecting";
    default:
      return "Unknown";
    }
  }

  void Conn::start_() {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");

    if (handle_ == invalid_socket) { return; }
    if (connected()) { return; }

    RELEASE_STORE(cleanup_done_, false);

    if (!read_buffer_) { read_buffer_ = std::make_unique<Buffer>(); }
    if (!write_buffer_) { write_buffer_ = std::make_unique<Buffer>(); }

    channel_ = std::make_unique<Channel>(event_poll_, handle_);
    channel_->set_read_callback([this]() { handle_read_event_(); });
    channel_->set_write_callback([this]() { handle_write_event_(); });
    channel_->set_close_callback([this](int err) { handle_close_event_(err); });
    channel_->tie(shared_from_this());
    channel_->add_read_event();

    set_state_(State::kConnected);
  }

  void Conn::handle_read_event_() {
    bool has_new_data     = false;
    bool should_close     = false;
    int  close_reason_err = 0;

    while (true) {
      if (!can_read_()) { return; }

      if (read_buffer_->writable_size() == 0) {
        read_buffer_->ensure_writable_size(1024 * 2);
      }

      int read_n = ::recv(handle_, read_buffer_->to_write(), read_buffer_->writable_size(), 0);
      if (read_n > 0) {
        read_buffer_->been_written(read_n);
        has_new_data = true;
        continue;
      }

      // 对端关闭
      if (read_n == 0) {
        should_close = true;
        break;
      }

      int err = Platform::get_last_error();
      switch (Platform::handle_error_action(err)) {
      case ErrorAction::kBreak: break;
      case ErrorAction::kContinue: continue;
      case ErrorAction::kClose:
        should_close     = true;
        close_reason_err = err;
        break;
      }

      break;
    }

    if (has_new_data && on_message_func_ != nullptr) { on_message_func_(read_buffer_.get()); }
    if (should_close) { handle_close_event_(close_reason_err); }
  }

  void Conn::handle_write_event_() {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");

    while (write_buffer_->readable_size() > 0) {
      size_t size   = write_buffer_->readable_size();
      int    send_n = ::send(handle_, write_buffer_->peek(), size, 0);
      if (send_n > 0) {
        write_buffer_->been_read(send_n);

        if (high_watermark_warning_ && write_buffer_->readable_size() <= low_watermark_) {
          if (watermark_func_ != nullptr) { watermark_func_(low_watermark_); }
          high_watermark_warning_ = false;
        }
        continue;
      }

      int         err    = Platform::get_last_error();
      ErrorAction action = Platform::handle_error_action(err);
      if (action == ErrorAction::kBreak) { break; }
      if (action == ErrorAction::kContinue) { continue; }

      handle_close_event_(err);
      return;
    }

    if (write_buffer_->readable_size() == 0) {
      write_buffer_->clear();
      channel_->remove_write_event();
      if (get_state_() == State::kDisconnecting) { Platform::shut_wr(handle_); }
    }
  }

  void Conn::handle_close_event_(int err) {
    if (get_state_() == State::kDisconnected) { return; }
    cleanup_(err);
  }

  void Conn::send_in_poll_thread_(const char* data, size_t size) {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");

    if (!connected() || write_buffer_ == nullptr || channel_ == nullptr) {
      return;
    }

    static constexpr size_t kDirectWriteBudget = 64 * 1024;

    if (write_buffer_->readable_size() > 0) {
      write_buffer_->append(data, size);
      channel_->add_write_event();
    } else {
      size_t sent_bytes        = 0;
      size_t direct_write_goal = (std::min)(size, kDirectWriteBudget);

      while (sent_bytes < direct_write_goal) {
        size_t attempt_size = direct_write_goal - sent_bytes;
        int    send_n       = ::send(handle_, data + sent_bytes, attempt_size, 0);
        if (send_n > 0) {
          sent_bytes += static_cast<size_t>(send_n);
          continue;
        }

        if (send_n == 0) { break; }

        int         err    = Platform::get_last_error();
        ErrorAction action = Platform::handle_error_action(err);
        if (action == ErrorAction::kBreak) { break; }
        if (action == ErrorAction::kContinue) { continue; }

        handle_close_event_(err);
        return;
      }

      if (sent_bytes < size) {
        write_buffer_->append(data + sent_bytes, size - sent_bytes);
        channel_->add_write_event();
      }
    }

    if (!high_watermark_warning_ && write_buffer_->readable_size() > high_watermark_) {
      if (watermark_func_ != nullptr) { watermark_func_(high_watermark_); }
      high_watermark_warning_ = true;
    }
  }
} // namespace cxpnet
