#ifndef CONN_H
#define CONN_H

#include "base_types_value.h"
#include "buffer.h"
#include <atomic>
#include <memory>

namespace cxpnet {
  class IOEventPoll;
  class Server;
  class Connector;
  class Channel;
  class Conn : public NonCopyable
      , public std::enable_shared_from_this<Conn> {
  public:
    using SendedCallback = std::function<void(bool)>;
    Conn(IOEventPoll* event_poll, int handle);
    ~Conn();

    void shutdown(); // graceful
    void close();    // force

    void set_remote_addr(const char* addr, uint16_t port) {
      memcpy(addr_, addr, INET6_ADDRSTRLEN);
      port_ = port;
    }
    std::pair<const char*, uint16_t> remote_addr_and_port() { return std::make_pair(addr_, port_); }
    int                              native_handle() const { return handle_; }
    bool                             connected() {
      return state_.load(std::memory_order_acquire) == static_cast<int>(State::kConnected);
    }
    void set_conn_user_callbacks(OnMessageCallback message_func, OnConnCloseCallback close_func) {
      on_message_func_ = message_func;
      on_close_func_   = close_func;
    }

    void send(const char* msg, size_t size);
    void send(const Buffer& msg) { send(msg.peek(), msg.readable_size()); }
    void send(std::string_view msg) { send(msg.data(), msg.size()); }

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
      if (watermark_func_ != nullptr) { watermark_func_ = std::move(watermark_func); }
    }
    // NOT thread-safe!
    // Only invoke this function in OnConnectionCallback
    void set_llf(bool llf) { llf_ = llf; }
  private:
    friend class cxpnet::IOEventPoll;
    friend class cxpnet::Server;
    friend class cxpnet::Connector;

    void _start();
    void _handle_read_event();
    void _handle_write_event();
    void _handle_close_event(int err);
    void _send_in_poll_thread(const char* data, size_t size);

    void  _set_state(State e) { state_.store(static_cast<int>(e), std::memory_order_release); }
    State _state() { return static_cast<State>(state_.load(std::memory_order_acquire)); }
    void  _set_on_close_holder_func(Closure holder_func) { on_close_holder_func_ = std::move(holder_func); }
  private:
    IOEventPoll*             event_poll_;
    int                      handle_;
    std::unique_ptr<Channel> channel_;
    OnMessageCallback        on_message_func_;
    OnConnCloseCallback      on_close_func_;
    Closure                  on_close_holder_func_;
    std::function<void(int)> watermark_func_;
    uint                     high_watermark_;
    uint                     low_watermark_;
    bool                     high_watermark_warning_;
    char                     addr_[INET6_ADDRSTRLEN];
    uint16_t                 port_;
    bool                     llf_; // last laxity first
    std::atomic<int>         state_;
    std::unique_ptr<Buffer>  read_buffer_;
    std::unique_ptr<Buffer>  write_buffer_;
  };
} // namespace cxpnet

#endif // CONN_H