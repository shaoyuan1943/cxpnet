#ifndef SAMPLE_CLIENT_H
#define SAMPLE_CLIENT_H

#include "conn.h"
#include "io_event_poll.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace cxpnet {
  class SampleClient : public NonCopyable {
  public:
    SampleClient(const char* addr, uint16_t port);
    ~SampleClient();

    bool connect();
    void run();
    void poll();

    void shutdown();
    void close();

    void send(const char* msg, size_t size);
    void send(std::string_view msg);

    bool    is_connected() const;
    ConnPtr conn() const;

    void set_conn_user_callback(std::function<void(ConnPtr)> func) {
      std::lock_guard<std::mutex> lock(mutex_);
      on_conn_func_ = std::move(func);
    }
    void set_error_user_callback(std::function<void(int)> func) {
      std::lock_guard<std::mutex> lock(mutex_);
      on_error_func_ = std::move(func);
    }
  private:
    enum class State {
      kCreated,
      kRunning,
      kClosing,
      kClosed,
    };

    bool is_active_() const;
    State get_state_() const { return ACQUIRE_LOAD(state_); }
    void connect_in_poll_();
    void on_connected_(ConnPtr conn);
    void on_connect_error_(int err);
  private:
    std::string                  addr_;
    uint16_t                     port_;
    IOEventPoll                  event_poll_ {};
    mutable std::mutex           mutex_ {};
    ConnPtr                      conn_ {nullptr};
    std::function<void(ConnPtr)> on_conn_func_ {nullptr};
    std::function<void(int)>     on_error_func_ {nullptr};
    std::atomic<State>           state_ {State::kCreated};
  };
} // namespace cxpnet

#endif // SAMPLE_CLIENT_H
