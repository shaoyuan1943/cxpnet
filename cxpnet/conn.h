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
  private:
    enum class State {
      kCreated,
      kConnecting,
      kConnected,
      kClosing,
      kClosed,
    };
  public:
    Conn(IOEventPoll* event_poll, int handle = invalid_socket);
    ~Conn();

    bool connect(const char* addr, uint16_t port,
                 std::function<void(ConnPtr)> on_connected,
                 std::function<void(int)>     on_connect_error = nullptr);

    bool connect_sync(const char* addr, uint16_t port);

    void shutdown();
    void close();
    void run_later_in_poll(Closure func);

    std::pair<const char*, uint16_t> remote_addr_and_port() const {
      return std::make_pair(addr_, port_);
    }
    int  native_handle() const { return handle_; }
    bool is_connected() const { return get_state_() == State::kConnected; }

    void set_message_callback(std::function<void(std::string_view)> message_func) {
      if (!message_func) {
        on_message_func_ = nullptr;
        return;
      }

      on_message_func_ = [message_func = std::move(message_func)](Buffer* buffer) {
        std::string_view data(buffer->readable_data(), buffer->readable_size());
        message_func(data);
        buffer->consume_all();
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
  private:
    friend class cxpnet::IOEventPoll;
    friend class cxpnet::Server;

    void start_();
    void handle_read_event_();
    void handle_write_event_();
    void handle_close_event_(int err);
    void send_in_poll_thread_(const char* data, size_t size);

    void  set_state_(State s) { RELEASE_STORE(state_, s); }
    State get_state_() const { return ACQUIRE_LOAD(state_); }
    bool  is_readable_() const {
      return get_state_() == State::kConnected || get_state_() == State::kClosing;
    }
    void set_internal_close_callback_(Closure&& close_callback) { internal_close_callback_ = std::move(close_callback); }
    void set_remote_addr_(const char* addr, uint16_t port) {
      memcpy(addr_, addr, INET6_ADDRSTRLEN);
      port_ = port;
    }

    // 关闭流程
    void enter_closing_in_poll_();
    void cancel_closing_timer_();
    void do_close_in_poll_(int err);
    void finish_close_(int err);

    void start_connect_in_poll_(const char* addr, uint16_t port);
    void handle_connect_event_();
    void retire_channel_();
  private:
    IOEventPoll*                 event_poll_;
    int                          handle_;
    std::unique_ptr<Channel>     channel_ {nullptr};
    std::function<void(Buffer*)> on_message_func_ {nullptr};
    std::function<void(int)>     on_close_func_ {nullptr};
    Closure                      internal_close_callback_ {nullptr};
    char                         addr_[INET6_ADDRSTRLEN] {0};
    uint16_t                     port_ {0};
    std::atomic<State>           state_ {State::kCreated};
    std::unique_ptr<Buffer>      read_buffer_ {nullptr};
    std::unique_ptr<Buffer>      write_buffer_ {nullptr};
    bool                         close_after_write_ {false};

    uint32_t                     graceful_close_timeout_ms_ {500};
    Timer::TimerID               close_timer_id_ {0};
    std::function<void(ConnPtr)> on_connected_func_ {nullptr};
    std::function<void(int)>     on_connect_error_func_ {nullptr};
  };
} // namespace cxpnet

#endif // CONN_H
