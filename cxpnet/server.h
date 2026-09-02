#ifndef SERVER_H
#define SERVER_H

#include "sock.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
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

    // 发起优雅关闭：停止 accept，逐连接发起优雅关闭；不等待收敛，
    // 需要观察进度时自行轮询 connection_count()
    void shutdown();
    // 立即关闭：强制关闭全部资源并 join poll 线程，返回时清理已完成。
    // 禁止在 poll 线程（含用户回调）中调用；回调里请用 shutdown()
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
      size_t count = 0;
      for (const auto& shard : conn_shards_) {
        std::lock_guard<std::mutex> lock(shard->mutex);
        count += shard->conns.size();
      }
      return count;
    }
  private:
    enum class State {
      kCreated,
      kRunning,
      kClosing,
      kClosed,
    };

    bool                 is_in_any_poll_thread_() const;
    std::vector<ConnPtr> snapshot_connections_();
    void                 close_polls_();
    State                get_state_() { return ACQUIRE_LOAD(state_); }

    void on_conn_close_(int shard_index, int handle);
    void on_acceptor_error_(int err);
    void on_poll_error_(IOEventPoll* event_poll, int err);
    void on_new_connection_(int handle, struct sockaddr_storage addr_storage);
  private:
    // 连接注册表按所属 poll 分片，避免高并发建连/断连时争抢同一把锁
    struct ConnShard {
      mutable std::mutex               mutex;
      std::unordered_map<int, ConnPtr> conns;
    };

    std::unique_ptr<IOEventPoll>              main_poll_ {nullptr};
    std::vector<std::unique_ptr<IOEventPoll>> sub_polls_;
    std::unique_ptr<Acceptor>                 acceptor_ {nullptr};
    std::unique_ptr<PollThreadPool>           poll_thread_pool_ {nullptr};

    int                thread_num_ {0};
    std::atomic<State> state_ {State::kCreated};
    RunningMode        running_mode_ {RunningMode::kOnePollPerThread};

    std::vector<std::unique_ptr<ConnShard>> conn_shards_;

    std::function<void(ConnPtr)> on_conn_func_ {nullptr};
    std::function<void(int)>     on_error_func_ {nullptr};

    size_t   max_connections_ {0}; // 0 表示无限制
    uint32_t graceful_close_timeout_ms_ {500};
  };
} // namespace cxpnet

#endif // SERVER_H
