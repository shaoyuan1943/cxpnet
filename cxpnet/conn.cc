#include "conn.h"
#include "channel.h"
#include "check.h"
#include "io_event_poll.h"
#include "platform_api.h"
#include "timer.h"

#include <atomic>
#include <future>
#include <memory>

namespace cxpnet {
  Conn::Conn(IOEventPoll* event_poll, int handle)
      : event_poll_ {event_poll}
      , handle_ {handle} {
  }

  Conn::~Conn() {
    cancel_closing_timer_();
    cancel_connect_timer_();
    if (handle_ != invalid_socket) {
      Platform::close_handle(handle_);
    }
  }

  bool Conn::connect(const char* addr, uint16_t port,
                     std::function<void(ConnPtr)> on_connected,
                     std::function<void(int)>     on_connect_error,
                     uint32_t                     timeout_ms) {
    State expected = State::kCreated;
    if (!state_.compare_exchange_strong(expected, State::kConnecting, std::memory_order_acq_rel)) {
      return false;
    }

    on_connected_func_     = std::move(on_connected);
    on_connect_error_func_ = std::move(on_connect_error);

    strncpy(addr_, addr, INET6_ADDRSTRLEN - 1);
    addr_[INET6_ADDRSTRLEN - 1] = '\0';
    port_                       = port;

    if (event_poll_->is_in_poll_thread()) {
      start_connect_in_poll_(addr, port, timeout_ms);
      return true;
    }

    auto self = shared_from_this();
    event_poll_->run_in_poll([self, addr_str = std::string(addr), port, timeout_ms]() {
      self->start_connect_in_poll_(addr_str.c_str(), port, timeout_ms);
    });

    return true;
  }

  bool Conn::connect_sync(const char* addr, uint16_t port, uint32_t timeout_ms) {
    State expected = State::kCreated;
    if (!state_.compare_exchange_strong(expected, State::kConnecting, std::memory_order_acq_rel)) {
      return false;
    }

    strncpy(addr_, addr, INET6_ADDRSTRLEN - 1);
    addr_[INET6_ADDRSTRLEN - 1] = '\0';
    port_                       = port;

    IPType        ip_type     = ip_address_type(std::string(addr));
    ProtocolStack proto_stack = (ip_type == IPType::kIPv4) ? ProtocolStack::kIPv4Only : ProtocolStack::kIPv6Only;
    if (ip_type == IPType::kInvalid) {
      set_state_(State::kClosed);
      return false;
    }

    struct sockaddr_storage addr_storage = Platform::get_sockaddr(addr, port, proto_stack);
    if (addr_storage.ss_family == 0) {
      set_state_(State::kClosed);
      return false;
    }

    int handle = Platform::connect(addr_storage, false, timeout_ms);
    if (handle < 0) {
      set_state_(State::kClosed);
      return false;
    }

    handle_ = handle;

    // poll 未在驱动时不存在并发事件处理，可以直接注册 channel
    if (event_poll_->is_in_poll_thread() || !event_poll_->is_polling()) {
      start_();
      return is_connected();
    }

    auto done   = std::make_shared<std::promise<bool>>();
    auto result = done->get_future();
    auto self   = shared_from_this();
    event_poll_->run_in_poll([self, done]() {
      if (self->get_state_() == State::kConnecting) { self->start_(); }
      done->set_value(self->is_connected());
    });

    return result.get();
  }

  void Conn::start_connect_in_poll_(const char* addr, uint16_t port, uint32_t timeout_ms) {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");
    if (get_state_() != State::kConnecting) { return; }

    IPType        ip_type     = ip_address_type(std::string(addr));
    ProtocolStack proto_stack = (ip_type == IPType::kIPv4) ? ProtocolStack::kIPv4Only : ProtocolStack::kIPv6Only;
    if (ip_type == IPType::kInvalid) {
      set_state_(State::kClosed);
      if (on_connect_error_func_) { on_connect_error_func_(EINVAL); }
      return;
    }

    struct sockaddr_storage addr_storage = Platform::get_sockaddr(addr, port, proto_stack);
    if (addr_storage.ss_family == 0) {
      set_state_(State::kClosed);
      if (on_connect_error_func_) { on_connect_error_func_(EINVAL); }
      return;
    }

    int handle = Platform::connect(addr_storage);
    if (handle < 0) {
      set_state_(State::kClosed);
      if (on_connect_error_func_) { on_connect_error_func_(Platform::get_last_error()); }
      return;
    }

    handle_  = handle;
    channel_ = std::make_unique<Channel>(event_poll_, handle_);
    channel_->set_write_callback([this]() { handle_connect_event_(); });
    channel_->set_close_callback([this](int) { handle_connect_event_(); });
    channel_->tie(shared_from_this());
    channel_->add_write_event();

    // 异步 connect 的超时兜底
    if (timeout_ms > 0 && event_poll_->timer_manager()) {
      std::weak_ptr<Conn> weak_self = shared_from_this();
      connect_timer_id_             = event_poll_->timer_manager()->add_timer(
          timeout_ms,
          [weak_self]() {
            if (auto self = weak_self.lock()) {
              self->event_poll_->run_in_poll([self]() {
                self->handle_connect_timeout_();
              });
            }
          });
    }
  }

  void Conn::handle_connect_timeout_() {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");
    if (get_state_() != State::kConnecting) { return; }

    connect_timer_id_ = 0;
    retire_channel_();

    if (handle_ != invalid_socket) {
      Platform::close_handle(handle_);
      handle_ = invalid_socket;
    }

    set_state_(State::kClosed);
    if (on_connect_error_func_) { on_connect_error_func_(ETIMEDOUT); }
  }

  void Conn::cancel_connect_timer_() {
    if (connect_timer_id_ != 0) {
      if (event_poll_ && event_poll_->timer_manager()) {
        event_poll_->timer_manager()->cancel_timer(connect_timer_id_);
      }

      connect_timer_id_ = 0;
    }
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
    cancel_connect_timer_();

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

      set_state_(State::kClosed);
      if (on_connect_error_func_) { on_connect_error_func_(err); }
      return;
    }

    retire_channel_();
    start_();

    if (on_connected_func_) { on_connected_func_(shared_from_this()); }
  }

  // 可能跨线程调用，shutdown 里面不要访问 handle_ 或 channel_
  void Conn::shutdown() {
    if (get_state_() == State::kConnecting) {
      close();
      return;
    }

    State state = get_state_();
    if (state != State::kConnected && state != State::kClosing) { return; }

    if (event_poll_->is_in_poll_thread()) {
      shutdown_in_poll_(state);
      return;
    }

    auto self = shared_from_this();
    event_poll_->run_in_poll([self]() {
      self->shutdown_in_poll_(self->get_state_());
    });
  }

  void Conn::shutdown_in_poll_(State state) {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");

    if (state == State::kConnected) {
      enter_closing_in_poll_();
      return;
    }

    // 对端 FIN 后冲刷中的连接不主动装定时器（慢速但存活的传输必须完整送达）；
    // 只在 shutdown 明确要求收敛时才补硬期限，避免 Server::shutdown 永远等不到它
    if (state == State::kClosing) { arm_closing_timer_(); }
  }

  void Conn::enter_closing_in_poll_() {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");

    if (!is_connected()) { return; }
    if (handle_ == invalid_socket) { return; }

    set_state_(State::kClosing);

    if (!write_buffer_ || write_buffer_->readable_size() == 0) {
      if (channel_) { channel_->remove_write_event(); }
      Platform::shut_wr(handle_);
    }

    arm_closing_timer_();
  }

  void Conn::arm_closing_timer_() {
    if (graceful_close_timeout_ms_ == 0) { return; }
    if (!event_poll_ || !event_poll_->timer_manager()) { return; }
    if (close_timer_id_ != 0) { return; }

    // 不要使用shared_ptr，会导致 Conn 残活在计时器里面
    std::weak_ptr<Conn> weak_self = shared_from_this();
    close_timer_id_               = event_poll_->timer_manager()->add_timer(
        graceful_close_timeout_ms_,
        [weak_self]() {
          if (auto self = weak_self.lock()) {
            self->event_poll_->run_in_poll([self]() {
              self->do_close_in_poll_(ETIMEDOUT);
            });
          }
        });
  }

  void Conn::close() {
    // 支持先 shutdown 后再 close：从优雅退出升级到强制退出
    State state = get_state_();
    if (state != State::kConnecting && state != State::kConnected && state != State::kClosing) { return; }

    if (event_poll_->is_in_poll_thread()) {
      do_close_in_poll_(0);
      return;
    }

    auto self = shared_from_this();
    event_poll_->run_in_poll([self]() {
      self->do_close_in_poll_(0);
    });
  }

  void Conn::run_later_in_poll(Closure func) {
    event_poll_->run_later(std::move(func));
  }

  void Conn::do_close_in_poll_(int err) {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");

    State old_state = state_.exchange(State::kClosed, std::memory_order_acq_rel);
    if (old_state == State::kClosed) { return; }
    if (handle_ == invalid_socket && !channel_) { return; }

    cancel_closing_timer_();
    cancel_connect_timer_();
    finish_close_(err);
  }

  void Conn::finish_close_(int err) {
    if (channel_) { channel_->unregister(); }

    if (handle_ != invalid_socket) {
      Platform::close_handle(handle_);
      handle_ = invalid_socket;
    }

    auto internal_close_callback = std::move(internal_close_callback_);
    auto close_func              = std::move(on_close_func_);

    if (internal_close_callback) { internal_close_callback(); }
    if (close_func) { close_func(err); }

    // 用户回调可能强捕获 Conn 自身；连接已进入终态，清空回调打断引用环
    on_message_func_        = nullptr;
    on_connected_func_      = nullptr;
    on_connect_error_func_  = nullptr;

    if (channel_) {
      Channel* raw_channel    = channel_.release();
      auto     channel_shared = std::shared_ptr<Channel>(raw_channel);
      event_poll_->run_later([channel_shared]() {});
    }
  }

  void Conn::cancel_closing_timer_() {
    if (close_timer_id_ != 0) {
      if (event_poll_ && event_poll_->timer_manager()) {
        event_poll_->timer_manager()->cancel_timer(close_timer_id_);
      }

      close_timer_id_ = 0;
    }
  }

  void Conn::send(const char* msg, size_t size) {
    if (!is_connected() || msg == nullptr || size == 0) { return; }

    if (event_poll_->is_in_poll_thread()) {
      send_in_poll_thread_(msg, size);
      return;
    }

    auto to_send = std::string(msg, size);
    event_poll_->run_in_poll([self = shared_from_this(), to_send = std::move(to_send)]() {
      self->send_in_poll_thread_(to_send.data(), to_send.size());
    });
  }

  void Conn::send(std::string_view msg) {
    send(msg.data(), msg.size());
  }

  std::string Conn::state_string() const {
    switch (get_state_()) {
    case State::kCreated: return "Created";
    case State::kConnecting: return "Connecting";
    case State::kConnected: return "Connected";
    case State::kClosing: return "Closing";
    case State::kClosed: return "Closed";
    default: return "Unknown";
    }
  }

  void Conn::start_() {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");

    if (handle_ == invalid_socket) { return; }
    if (is_connected()) { return; }

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
    bool peer_closed      = false;
    bool should_close     = false;
    int  close_reason_err = 0;

    while (true) {
      if (!is_readable_()) { return; }

      if (read_buffer_->writable_size() == 0) {
        read_buffer_->ensure_writable_size(1024 * 2);
      }

      int read_n = ::recv(handle_, read_buffer_->writable_data(), read_buffer_->writable_size(), 0);
      if (read_n > 0) {
        read_buffer_->commit_write(read_n);
        has_new_data = true;
        continue;
      }

      // 收到对端FIN，本端读关闭，但可能还可写
      if (read_n == 0) {
        peer_closed = true;
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

    // 关闭本端读
    if (peer_closed) {
      close_after_write_ = true;
      if (channel_) { channel_->remove_read_event(); }
    }

    if (has_new_data && on_message_func_ != nullptr) { on_message_func_(read_buffer_.get()); }

    if (should_close) {
      handle_close_event_(close_reason_err);
      return;
    }

    if (!peer_closed || get_state_() == State::kClosed) { return; }

    set_state_(State::kClosing);
    if (!write_buffer_ || write_buffer_->readable_size() == 0) {
      do_close_in_poll_(0);
    }
  }

  void Conn::handle_write_event_() {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");

    if (get_state_() == State::kClosed) { return; }

    while (write_buffer_->readable_size() > 0) {
      size_t size   = write_buffer_->readable_size();
      int    send_n = Platform::send(handle_, write_buffer_->readable_data(), size);
      if (send_n > 0) {
        write_buffer_->consume(send_n);
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
      if (close_after_write_) {
        do_close_in_poll_(0);
        return;
      }

      // 关闭过程中，IOEventPoll 还在驱动中，写完之后进行写端关闭
      if (get_state_() == State::kClosing) { Platform::shut_wr(handle_); }
    }
  }

  void Conn::handle_close_event_(int err) {
    if (get_state_() == State::kClosed) { return; }
    do_close_in_poll_(err);
  }

  void Conn::send_in_poll_thread_(const char* data, size_t size) {
    CXPNET_CHECK(event_poll_->is_in_poll_thread(), "Must in IO thread");

    if (!is_connected() || write_buffer_ == nullptr || channel_ == nullptr) {
      return;
    }

    if (write_buffer_->readable_size() > 0) {
      write_buffer_->append(data, size);
      channel_->add_write_event();
    } else {
      size_t sent_bytes = 0;

      while (sent_bytes < size) {
        size_t attempt_size = size - sent_bytes;
        int    send_n       = Platform::send(handle_, data + sent_bytes, attempt_size);
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
  }
} // namespace cxpnet
