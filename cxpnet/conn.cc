#include "conn.h"
#include "platform_api.h"
#include "channel.h"
#include "io_event_poll.h"
#include <atomic>
#include <memory>

namespace cxpnet {
  Conn::Conn(IOEventPoll* event_poll, int handle) {
    handle_     = handle;
    event_poll_ = event_poll;
  }

  Conn::~Conn() { Platform::close_handle(handle_); }

  void Conn::shutdown() {
    if (!connected()) { return; }
    _set_state(State::kDisconnecting);

    std::shared_ptr<Conn> shared_this = shared_from_this();
    event_poll_->run_in_poll([shared_this]() {
      if (!shared_this->channel_->writing()) { Platform::shut_wr(shared_this->handle_); }
    });
  }

  void Conn::close() {
    if (!connected()) { return; }
    _set_state(State::kDisconnecting);

    if (event_poll_->is_in_poll_thread()) {
      _handle_close_event(0);
    } else {
      std::shared_ptr<Conn> shared_this = shared_from_this();
      event_poll_->run_in_poll(std::bind(&Conn::_handle_close_event, shared_this, 0));
    }
  }

  void Conn::send(const char* msg, size_t size) {
    if (!connected() || msg == nullptr || size == 0) { return; }

    if (event_poll_->is_in_poll_thread()) {
      _send_in_poll_thread(msg, size);
    } else {
      event_poll_->run_in_poll([self = shared_from_this(), to_send = std::string(msg)]() {
        if (!self->llf_) {
          self->write_buffer_->append(to_send.data(), to_send.size());
          if (self->write_buffer_->readable_size() <= to_send.size()) {
            self->channel_->add_write_event();
          }
          return;
        }

        self->_send_in_poll_thread(to_send.data(), to_send.size());
      });
    }
  }

  void Conn::_start() {
    if (connected()) { return; }

    read_buffer_  = std::make_unique<Buffer>();
    write_buffer_ = std::make_unique<Buffer>();
    channel_      = std::make_unique<Channel>(event_poll_, handle_);
    channel_->set_read_callback(std::bind(&Conn::_handle_read_event, shared_from_this()));
    channel_->set_write_callback(std::bind(&Conn::_handle_write_event, shared_from_this()));
    channel_->set_close_callback(std::bind(&Conn::_handle_close_event, shared_from_this(), std::placeholders::_1));
    channel_->add_read_event();
    channel_->tie(shared_from_this());
    state_.store(static_cast<int>(State::kConnected), std::memory_order_release);
  }

  void Conn::_handle_read_event() {
    int read_n     = -1;
    int read_total = 0;
    while (true) {
      if (read_buffer_->writable_size() <= 0) { read_buffer_->ensure_writable_size(1024 * 2); }

      read_n = ::recv(handle_, read_buffer_->begin_write(), read_buffer_->writable_size(), 0);
      if (read_n > 0) {
        read_total += read_n;
        read_buffer_->been_written(read_n);
        if (on_message_func_ != nullptr) {
          on_message_func_(shared_from_this(), read_buffer_.get());
        }

        continue;
      }

      if (read_n == 0) {
        if (static_cast<State>(state_.load(std::memory_order_acquire)) == State::kDisconnecting) {
          _handle_close_event(0);
          return;
        }
      }

      int err = Platform::get_last_error();
      if (Platform::handle_error_action(err) == ErrorAction::kBreak) { break; }
      if (Platform::handle_error_action(err) == ErrorAction::kContinue) { continue; }

      _handle_close_event(err);
      return;
    }
  }

  void Conn::_handle_write_event() {
    while (write_buffer_->readable_size() > 0) {
      size_t size   = write_buffer_->readable_size();
      int    send_n = ::send(handle_, write_buffer_->peek(), size, 0);
      if (send_n > 0) {
        write_buffer_->been_read(send_n);

        // high watermark warning
        if (high_watermark_warning_) {
          if (write_buffer_->readable_size() <= low_watermark_) {
            if (watermark_func_ != nullptr) { watermark_func_(low_watermark_); }
            high_watermark_warning_ = false;
          }
        }
      } else {
        int err = Platform::get_last_error();
        if (Platform::handle_error_action(err) == ErrorAction::kBreak) { break; }
        if (Platform::handle_error_action(err) == ErrorAction::kContinue) { continue; }

        _handle_close_event(err);
        return;
      }
    }

    // The 'shut_wr' operation may be executed repeatedly,
    // but calling 'shut_wr' again on a socket that has already been shut is harmless
    if (write_buffer_->readable_size() == 0) {
      write_buffer_->clear();
      channel_->remove_write_event();
      if (static_cast<State>(state_.load(std::memory_order_acquire)) == State::kDisconnecting) {
        Platform::shut_wr(handle_);
      }
    }
  }
  
  void Conn::_handle_close_event(int err) {
    int expected_state = static_cast<int>(State::kConnected);
    if (!state_.compare_exchange_strong(expected_state, static_cast<int>(State::kDisconnecting))) {
      return;
    }

    channel_->clear_event();
    channel_->remove();

    if (on_close_func_ != nullptr) {
      std::shared_ptr<Conn> shared_this = shared_from_this();
      on_close_func_(shared_this, err);
    }

    if (on_close_holder_func_ != nullptr) {
      on_close_holder_func_();
    }

    _set_state(State::kDisconnected);
  }

  void Conn::_send_in_poll_thread(const char* data, size_t size) {
    if (write_buffer_->readable_size() > 0) { // wait next poll
      write_buffer_->append(data, size);
    } else {
      int send_n = ::send(handle_, data, size, 0);
      if (send_n > 0) {
        if (size - send_n > 0) { // wait next poll
          write_buffer_->append(data + send_n, size - send_n);
          channel_->add_write_event();
        }
      }

      if (send_n == -1) {
        int err = Platform::get_last_error();
        if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR) { // wait next poll
          write_buffer_->append(data, size);
          channel_->add_write_event();
        } else {
          _handle_close_event(err);
          return;
        }
      }
    }

    if (write_buffer_->readable_size() > high_watermark_) {
      if (watermark_func_ != nullptr) { watermark_func_(high_watermark_); }
      high_watermark_warning_ = true;
    }
  }
} // namespace cxpnet