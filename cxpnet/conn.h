#ifndef CONN_H
#define CONN_H

#include "buffer.h"
#include "channel.h"
#include "io_base.h"
#include "io_event_poll.h"
#include "platform_api.h"
#include <atomic>
#include <memory>

namespace cxpnet {
  class Conn : public std::enable_shared_from_this<Conn> {
  public:
    Conn(IOEventPoll* event_poll, int handle) {
      handle_       = handle;
      event_poll_   = event_poll;
      read_buffer_  = std::make_unique<SimpleBuffer>();
      write_buffer_ = std::make_unique<SimpleBuffer>();
      channel_      = std::make_unique<Channel>(event_poll, handle);
      channel_->set_read_callback(std::bind(&Conn::_handle_read_event, shared_from_this()));
      channel_->set_write_callback(std::bind(&Conn::_handle_write_event, shared_from_this()));
      channel_->set_close_callback(std::bind(&Conn::_handle_close_event, shared_from_this(), std::placeholders::_1));
      channel_->add_read_event();
      channel_->tie(shared_from_this());
      state_.store(static_cast<int>(State::kConnected), std::memory_order_release);
    }
    ~Conn() {
      platform::close_handle(handle_);
    }
    // graceful
    void shutdown() {
      if (!connected()) { return; }
      _set_state(State::kDisconnecting);

      std::shared_ptr<Conn> shared_this = shared_from_this();
      event_poll_->run_in_poll([shared_this]() {
        if (!shared_this->channel_->writing()) { platform::shut_wr(shared_this->handle_); }
      });
    }
    // force
    void close() {
      if (!connected()) { return; }
      _set_state(State::kDisconnecting);

      if (event_poll_->is_in_poll_thread()) {
        _handle_close_event(0);
      } else {
        std::shared_ptr<Conn> shared_this = shared_from_this();
        event_poll_->run_in_poll(std::bind(&Conn::_handle_close_event, shared_this, 0));
      }
    }

    void set_remote_addr(const char* addr, uint16_t port) {
      memcpy(addr_, addr, INET6_ADDRSTRLEN);
      port_ = port;
    }
    std::pair<const char*, uint16_t> remote_addr_and_port() { return std::make_pair(addr_, port_); }
    int                              native_handle() { return handle_; }
    bool                             connected() {
      return state_.load(std::memory_order_acquire) == static_cast<int>(State::kConnected);
    }

    void set_conn_callbacks(OnMessageCallback message_func, OnCloseCallback close_func) {
      on_message_func_ = message_func;
      on_close_func_   = close_func;
    }

    void send(const std::string&        msg,
              std::function<void(bool)> op_completed_func = nullptr) {
      send(msg.data(), msg.size(), op_completed_func);
    }
    void send(const std::string_view    msg,
              std::function<void(bool)> op_completed_func = nullptr) {
      send(msg.data(), msg.size(), op_completed_func);
    }
    void send(const char* msg, size_t size,
              std::function<void(bool)> op_completed_func = nullptr) {
      if (event_poll_->is_in_poll_thread()) {
        _send_in_poll_thread(msg, size, std::move(op_completed_func));
      } else {
        // TODO: Use std::shared_ptr to transmit data to poll
        // auto copied_data = std::make_shared<std::vector<char>>(msg, msg + size);
        event_poll_->run_in_poll([this, msg, size, completed_func = std::move(op_completed_func)]() {
          _send_in_poll_thread(msg, size, completed_func);
        });
      }
    }

    // NOT thread-safe！
    // Only invoke this function in OnConnectionCallback
    void set_read_write_buffer_size(uint read_size, uint write_size) {
      if (read_size != 0 && write_size != 0) {
        read_buffer_.reset(new SimpleBuffer(read_size));
        write_buffer_.reset(new SimpleBuffer(write_size));
      }
    }
    // NOT thread-safe！
    // Only invoke this function in OnConnectionCallback
    void set_watermark(uint high, uint low) {
      if (high == low) { return; }
      if (high == 0 || low == 0) { return; }

      high_watermark_ = high;
      low_watermark_  = low;
    }
    // NOT thread-safe！
    // Only invoke this function in OnConnectionCallback
    void set_watermark_callback(std::function<void(int)> watermark_func) {
      if (watermark_func_ != nullptr) { watermark_func_ = std::move(watermark_func); }
    }
  private:
    friend class IOEventPoll;
    // clang-format off
    enum class State { kDisconnected, kConnecting, kConnected, kDisconnecting };
    // clang-format on

    void _handle_read_event() {
      int read_n       = -1;
      int readed_total = 0;
      while (true) {
        if (read_buffer_->writable_size() <= 0) { read_buffer_->ensure_writable_size(max_size_per_read); }

        read_n = ::recv(handle_, read_buffer_->take_data(), read_buffer_->writable_size(), 0);
        if (read_n > 0) {
          readed_total += read_n;
          read_buffer_->add_written_size_from_external(read_n);
          if (on_message_func_ != nullptr) {
            on_message_func_(shared_from_this(), read_buffer_->take_data(), read_buffer_->writable_size());
          }

          read_buffer_->clear();
          continue;
        }

        if (read_n == 0) {
          if (static_cast<State>(state_.load(std::memory_order_acquire)) == State::kDisconnecting) {
            _handle_close_event(0); // User manually closed: shutdown
            return;
          }
        }

        int err = platform::get_last_error();
        if (platform::handle_error_action(err) == platform::ErrorAction::kBreak) { break; }
        if (platform::handle_error_action(err) == platform::ErrorAction::kContinue) { continue; }

        _handle_close_event(err);
        return;
      }
    }
    void _handle_write_event() {
      size_t size   = write_buffer_->written_size_from_seek();
      int    send_n = ::send(handle_, write_buffer_->take_data_from_seek(), size, 0);
      if (send_n > 0) {
        write_buffer_->seek(send_n);

        // high watermark warning
        if (high_watermark_warning_) {
          if (write_buffer_->written_size_from_seek() <= low_watermark_) {
            if (watermark_func_ != nullptr) { watermark_func_(low_watermark_); }
            high_watermark_warning_ = false;
          }
        }

        if (write_buffer_->written_size_from_seek() == 0) {
          write_buffer_->clear();
          channel_->remove_write_event();
          if (static_cast<State>(state_.load(std::memory_order_acquire)) == State::kDisconnecting) {
            platform::shut_wr(handle_);
          }
        }
      }

      if (send_n == -1) {
        int err = platform::get_last_error();
        if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR) { return; } // wait for next poll
        _handle_close_event(err);
      }
    }
    void _handle_close_event(int err) {
      _set_state(State::kDisconnected);
      channel_->clear_event();
      channel_->remove();

      if (on_close_func_ != nullptr) {
        std::shared_ptr<Conn> shared_this = shared_from_this();
        on_close_func_(shared_this, err);
      }
    }

    // TODO: 简化一下，缩减为一个函数，去掉重载
    bool _send_in_poll_thread(const char* data, size_t size, std::function<void(bool)> op_completed_func) {
      int op_completed = _send_in_poll_thread(data, size);
      if (op_completed_func != nullptr) { op_completed_func(op_completed); }
      return op_completed;
    }
    bool _send_in_poll_thread(const char* data, size_t size) {
      if (!connected() || size <= 0) { return false; }

      bool wait_next_poll = false;
      if (write_buffer_->written_size_from_seek() > 0) { // wait next poll
        write_buffer_->write(data, size);
        wait_next_poll = true;
      } else {
        int send_n = ::send(handle_, data, size, 0);
        if (send_n > 0) {
          if (size - send_n > 0) { // wait next poll
            write_buffer_->write(data + send_n, size - send_n);
            channel_->add_write_event();
            wait_next_poll = true;
          }
        }

        if (send_n == -1) {
          int err = platform::get_last_error();
          if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR) { // wait next poll
            write_buffer_->write(data, size);
            channel_->add_write_event();
            wait_next_poll = true;
          } else {
            _handle_close_event(err);
          }
        }
      }

      if (wait_next_poll) {
        if (write_buffer_->written_size_from_seek() > high_watermark_) {
          if (watermark_func_ != nullptr) { watermark_func_(high_watermark_); }
          high_watermark_warning_ = true;
        }
      }

      return wait_next_poll;
    }

    void  _set_state(State e) { state_.store(static_cast<int>(e), std::memory_order_release); }
    State _state() { return static_cast<State>(state_.load(std::memory_order_acquire)); }
  private:
    int                           handle_     = -1;
    IOEventPoll*                  event_poll_ = nullptr;
    std::unique_ptr<Channel>      channel_    = nullptr;
    std::atomic<int>              state_;
    OnMessageCallback             on_message_func_ = nullptr;
    OnCloseCallback               on_close_func_   = nullptr;
    std::unique_ptr<SimpleBuffer> read_buffer_;
    std::unique_ptr<SimpleBuffer> write_buffer_;
    std::function<void(int)>      watermark_func_         = nullptr;
    uint                          high_watermark_         = 1024 * 1024;
    uint                          low_watermark_          = 256 * 1024;
    bool                          high_watermark_warning_ = false;
    char                          addr_[INET6_ADDRSTRLEN] = {0};
    uint16_t                      port_                   = 0;
  };
} // namespace cxpnet

#endif // CONN_H