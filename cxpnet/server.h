#ifndef SERVER_H
#define SERVER_H

#include "sock.h"
#include <chrono>
#include <atomic>
#include <functional>
#include <future>
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

  // TCP 服务器
  // 对齐 iocpnet 的 IOCPServer 接口
  class Server : public NonCopyable {
  public:
    Server(const char* addr, uint16_t port,
           ProtocolStack proto_stack = ProtocolStack::kIPv4Only,
           int           option      = SocketOption::kNone);
    ~Server();

    // 优雅关闭：停止 accept → 优雅关闭连接 → 等待完成或超时 → 释放资源
    void shutdown();
    // 立即关闭：立即释放所有资源
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
    void set_shutdown_timeout(uint32_t ms) {
      shutdown_timeout_ms_ = ms;
    }
    size_t connection_count() const {
      std::lock_guard<std::mutex> lock(conns_mutex_);
      return conns_.size();
    }
  private:
    void shutdown_impl_();
    void close_impl_();
    void shutdown_all_one_thread_();
    void finish_shutdown_all_one_thread_();
    void run_in_poll_and_wait_(IOEventPoll* event_poll, Closure func);
    bool is_in_managed_poll_thread_() const;
    void start_exit_thread_(Closure func);
    void join_exit_thread_();
    void on_conn_close_(int handle);
    void on_acceptor_error_(int err);
    void on_poll_error_(IOEventPoll* event_poll, int err);
    void on_new_connection_(int handle, struct sockaddr_storage addr_storage);
    void shutdown_polls_();
  private:
    std::unique_ptr<IOEventPoll>              main_poll_;
    std::vector<std::unique_ptr<IOEventPoll>> sub_polls_;
    std::unique_ptr<Acceptor>                 acceptor_;
    std::unique_ptr<PollThreadPool>           poll_thread_pool_;
    std::thread                               exit_thread_;
    mutable std::mutex                        exit_thread_mutex_;

    int               thread_num_ = 0;
    std::atomic<bool> started_ {false};
    std::atomic<bool> shutting_down_ {false};
    RunningMode       running_mode_;
    std::chrono::steady_clock::time_point shutdown_deadline_ {};

    std::unordered_map<int, std::shared_ptr<Conn>> conns_;
    mutable std::mutex                             conns_mutex_;

    std::function<void(ConnPtr)> on_conn_func_;
    std::function<void(int)>     on_error_func_;

    size_t   max_connections_     = 0;     // 0 表示无限制
    uint32_t shutdown_timeout_ms_ = 30000; // 默认 30 秒
  };
} // namespace cxpnet

#endif // SERVER_H
