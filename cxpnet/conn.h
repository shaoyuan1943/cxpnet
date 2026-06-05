#ifndef CONN_H
#define CONN_H

#include "buffer.h"
#include "sock.h"
#include "timer.h"

#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <string_view>

namespace cxpnet {
  class IOEventPoll;
  class Server;
  class Channel;

  class Conn : public NonCopyable
      , public std::enable_shared_from_this<Conn> {
  public:
    Conn(IOEventPoll* event_poll, int handle = invalid_socket);
    ~Conn();

    void connect(const char* addr, uint16_t port,
                 std::function<void(ConnPtr)> on_connected,
                 std::function<void(int)>     on_connect_error = nullptr);

    bool connect_sync(const char* addr, uint16_t port);

    void shutdown();
    void close();

    std::pair<const char*, uint16_t> remote_addr_and_port() const {
      return std::make_pair(addr_, port_);
    }
    int  native_handle() const { return handle_; }
    bool connected() const { return get_state_() == State::kConnected; }

    void set_message_callback(std::function<void(std::string_view)> message_func) {
      if (!message_func) {
        on_message_func_ = nullptr;
        return;
      }

      on_message_func_ = [message_func = std::move(message_func)](Buffer* buffer) {
        std::string_view data(buffer->peek(), buffer->readable_size());
        message_func(data);
        buffer->been_read_all();
      };
    }

    void set_message_callback(std::function<void(Buffer*)> message_func) {
      on_message_func_ = std::move(message_func);
    }

    void set_close_callback(std::function<void(int)> close_func) {
      on_close_func_ = std::move(close_func);
    }

    void set_graceful_close_timeout(uint32_t ms) {
      graceful_close_timeout_ms_ = ms;
    }

    void send(const char* msg, size_t size);
    void send(std::string_view msg);

    std::string state_string();

    // NOT thread-safe！
    // Only invoke this function in OnConnectionCallback
    void set_read_write_buffer_size(uint read_size, uint write_size) {
      if (read_size != 0 && write_size != 0) {
        read_buffer_.reset(new Buffer(read_size));
        write_buffer_.reset(new Buffer(write_size));
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
      if (watermark_func) { watermark_func_ = std::move(watermark_func); }
    }
  private:
    friend class cxpnet::IOEventPoll;
    friend class cxpnet::Server;

    void start_();
    void handle_read_event_();
    void handle_write_event_();
    void handle_close_event_(int err);
    void send_in_poll_thread_(const char* data, size_t size);

    void  set_state_(State s) { RELEASE_STORE(state_, static_cast<int>(s)); }
    State get_state_() const { return static_cast<State>(ACQUIRE_LOAD(state_)); }
    bool  can_read_() const {
      return get_state_() == State::kConnected || get_state_() == State::kDisconnecting;
    }
    void set_internal_close_callback_(Closure&& close_callback) { internal_close_callback_ = std::move(close_callback); }
    void set_remote_addr_(const char* addr, uint16_t port) {
      memcpy(addr_, addr, INET6_ADDRSTRLEN);
      port_ = port;
    }

    // 关闭流程
    void shutdown_in_poll_();
    void cleanup_(int err);
    void do_cleanup_(int err);
    void start_graceful_close_timeout_();
    void cancel_graceful_close_timeout_();

    void start_connect_in_poll_(const char* addr, uint16_t port);
    void handle_connect_event_();
    void retire_channel_();
  private:
    IOEventPoll*                 event_poll_;
    int                          handle_;
    std::unique_ptr<Channel>     channel_;
    std::function<void(Buffer*)> on_message_func_;
    std::function<void(int)>     on_close_func_;
    Closure                      internal_close_callback_;
    std::function<void(int)>     watermark_func_;
    uint                         high_watermark_;
    uint                         low_watermark_;
    bool                         high_watermark_warning_;
    char                         addr_[INET6_ADDRSTRLEN];
    uint16_t                     port_;
    std::atomic<int>             state_;
    std::unique_ptr<Buffer>      read_buffer_;
    std::unique_ptr<Buffer>      write_buffer_;

    uint32_t          graceful_close_timeout_ms_ = 5000; // 默认 5 秒
    Timer::TimerID    close_timer_id_            = 0;
    std::atomic<bool> cleanup_done_ {false};

    std::function<void(ConnPtr)> on_connected_func_;
    std::function<void(int)>     on_connect_error_func_;
  };
} // namespace cxpnet

#endif // CONN_H
