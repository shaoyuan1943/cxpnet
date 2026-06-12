#ifndef SERVER_H
#define SERVER_H

#include "sock.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace cxpnet {
  class Acceptor;
  class Conn;
  class IOEventPoll;
  class PollThreadPool;

  class Server : public NonCopyable {
  public:
    Server(const char* addr, uint16_t port,
           ProtocolStack proto_stack = ProtocolStack::kIPv4Only,
           int           option      = SocketOption::kNone);
    ~Server();

    // graceful close flow
    void shutdown();
    // close immediately
    void close();

    bool start(RunningMode mode);
    void run();  // blocking
    void poll(); // non-blocking

    void set_thread_num(int n) { thread_num_ = n; }

    void set_conn_user_callback(std::function<void(ConnPtr)> func) {
      on_conn_func_ = std::move(func);
    }
    void set_error_user_callback(std::function<void(int)> func) {
      on_error_func_ = std::move(func);
    }
    void set_max_connections(size_t max_connections) {
      max_connections_ = max_connections;
    }
    void set_graceful_close_timeout(uint32_t ms) {
      graceful_close_timeout_ms_ = ms;
    }
    size_t connection_count() const {
      std::lock_guard<std::mutex> lock(conns_mutex_);
      return conns_.size();
    }
  private:
    enum class State {
      kCreated,
      kRunning,
      kClosing,
      kClosed,
    };

    bool                 try_enter_closing_();
    std::vector<ConnPtr> snapshot_connections_();
    void                 try_finish_close_();
    void                 finish_close_();
    void                 wait_until_closed_();
    bool                 is_in_sub_poll_thread_() const;
    void                 start_close_thread_(Closure func);
    void                 join_closing_thread_();
    void                 close_polls_();
    State                get_state_() { return ACQUIRE_LOAD(state_); }

    void on_conn_close_(int handle);
    void on_acceptor_error_(int err);
    void on_poll_error_(IOEventPoll* event_poll, int err);
    void on_new_connection_(int handle, struct sockaddr_storage addr_storage);
  private:
    std::unique_ptr<IOEventPoll>              main_poll_ {nullptr};
    std::vector<std::unique_ptr<IOEventPoll>> sub_polls_;
    std::unique_ptr<Acceptor>                 acceptor_ {nullptr};
    std::unique_ptr<PollThreadPool>           poll_thread_pool_ {nullptr};
    std::thread                               closing_thread_ {};
    mutable std::mutex                        closing_thread_mutex_;

    int                thread_num_ {0};
    std::atomic<State> state_ {State::kCreated};
    std::atomic<bool>  close_polls_flag_ {false};
    RunningMode        running_mode_ {RunningMode::kOnePollPerThread};

    std::unordered_map<int, ConnPtr> conns_;
    mutable std::mutex               conns_mutex_;

    std::function<void(ConnPtr)> on_conn_func_ {nullptr};
    std::function<void(int)>     on_error_func_ {nullptr};

    size_t   max_connections_ {0};             // 0 表示无限制
    uint32_t graceful_close_timeout_ms_ {500}; // 默认 5 秒
  };
} // namespace cxpnet

#endif // SERVER_H
