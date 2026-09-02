#include "server.h"
#include "acceptor.h"
#include "check.h"
#include "conn.h"
#include "io_event_poll.h"
#include "poll_thread_pool.h"
#include "sock.h"

#include <format>

namespace cxpnet {
  Server::Server(const char* addr, uint16_t port, ProtocolStack proto_stack, int option) {
    main_poll_ = std::make_unique<IOEventPoll>();
    main_poll_->set_error_callback(std::bind(&Server::on_poll_error_, this, std::placeholders::_1, std::placeholders::_2));
    main_poll_->set_name("main_poll");

    acceptor_ = std::make_unique<Acceptor>(main_poll_.get());
    acceptor_->set_listen_addr(addr, port, proto_stack, option);
    acceptor_->set_new_conn_callback(std::bind(&Server::on_new_connection_, this, std::placeholders::_1, std::placeholders::_2));
    acceptor_->set_error_callback(std::bind(&Server::on_acceptor_error_, this, std::placeholders::_1));
  }

  Server::~Server() {
    // 析构会 join poll 线程并释放 poll 资源；在 poll 线程的事件循环内析构
    // 等于 join 自己或访问正在执行的对象。
    // Release 下 CHECK 以抛异常失败，析构内抛异常即 terminate——这正是
    // 违例时想要的 fail-fast 行为，抑制编译器告警即可
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wterminate"
#endif
    CXPNET_CHECK(!is_in_any_poll_thread_(), "Server must not be destroyed on a poll thread");
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    close();
  }

  void Server::shutdown() {
    State expected = State::kRunning;
    if (!state_.compare_exchange_strong(expected, State::kClosing, std::memory_order_acq_rel)) { return; }

    if (acceptor_) { acceptor_->close(); }

    for (auto& conn : snapshot_connections_()) {
      conn->set_graceful_close_timeout(graceful_close_timeout_ms_);
      conn->shutdown();
    }
  }

  void Server::close() {
    // close 会 join poll 线程；在 poll 线程里调用等于 join 自己。
    // 回调里想关停服务器请用 shutdown()，close/析构留给控制线程
    CXPNET_CHECK(!is_in_any_poll_thread_(), "Server::close must not be called on a poll thread");

    State state = get_state_();
    while (state == State::kRunning || state == State::kClosing) {
      if (state_.compare_exchange_weak(state, State::kClosed, std::memory_order_acq_rel)) { break; }
    }

    if (state == State::kCreated || state == State::kClosed) { return; }
    if (acceptor_) { acceptor_->close(); }

    for (auto& conn : snapshot_connections_()) {
      conn->close();
    }

    close_polls_();
  }

  std::vector<ConnPtr> Server::snapshot_connections_() {
    std::vector<ConnPtr> conns_snapshot;

    for (const auto& shard : conn_shards_) {
      std::lock_guard<std::mutex> lock(shard->mutex);
      for (auto& [handle, conn] : shard->conns) {
        if (conn) { conns_snapshot.push_back(conn); }
      }
    }

    return conns_snapshot;
  }

  bool Server::is_in_any_poll_thread_() const {
    // thread_id_ 在 poll 进入驱动前仍是创建线程的 id，is_polling() 用来排除这个窗口
    if (main_poll_ && main_poll_->is_in_poll_thread() && main_poll_->is_polling()) { return true; }

    for (const auto& poll : sub_polls_) {
      if (poll && poll->is_in_poll_thread() && poll->is_polling()) { return true; }
    }

    return false;
  }

  void Server::close_polls_() {
    if (poll_thread_pool_) { poll_thread_pool_->shutdown(); }
    if (main_poll_) { main_poll_->shutdown(); }
  }

  bool Server::start(RunningMode mode) {
    State expected = State::kCreated;
    if (!state_.compare_exchange_strong(expected, State::kRunning, std::memory_order_acq_rel)) {
      return false;
    }

    running_mode_ = mode;

    constexpr int kMaxThreadNum = 24;
    if (running_mode_ == RunningMode::kOnePollPerThread) {
      if (thread_num_ <= 0 || thread_num_ > kMaxThreadNum) {
        RELEASE_STORE(state_, State::kClosed);
        return false;
      }

      sub_polls_.reserve(thread_num_);
      std::vector<IOEventPoll*> polls;
      polls.reserve(thread_num_);
      for (int i = 0; i < thread_num_; ++i) {
        auto poll = std::make_unique<IOEventPoll>();
        poll->set_name(std::format("sub_poll_{}", i + 1));
        poll->set_error_callback(std::bind(&Server::on_poll_error_, this, std::placeholders::_1, std::placeholders::_2));

        polls.push_back(poll.get());
        sub_polls_.push_back(std::move(poll));
      }

      // PollThreadPool only use IOEventPoll, not control IOEventPoll
      poll_thread_pool_ = std::make_unique<PollThreadPool>(polls);
      poll_thread_pool_->start();

      conn_shards_.reserve(thread_num_);
      for (int i = 0; i < thread_num_; ++i) {
        conn_shards_.push_back(std::make_unique<ConnShard>());
      }
    } else {
      conn_shards_.push_back(std::make_unique<ConnShard>());
    }

    if (!acceptor_->listen()) {
      close_polls_();
      RELEASE_STORE(state_, State::kClosed);
      return false;
    }

    return true;
  }

  void Server::run() {
    CXPNET_CHECK(running_mode_ == RunningMode::kOnePollPerThread, "");

    State state = ACQUIRE_LOAD(state_);
    if (state == State::kCreated || state == State::kClosed) { return; }

    main_poll_->run();
  }

  void Server::poll() {
    CXPNET_CHECK(running_mode_ == RunningMode::kAllOneThread, "");

    State state = ACQUIRE_LOAD(state_);
    if (state == State::kCreated || state == State::kClosed) { return; }

    // kClosing 期间继续驱动：shutdown 进展（连接冲刷、关闭定时器）由 poll 推进
    main_poll_->poll();
  }

  void Server::on_conn_close_(int shard_index, int handle) {
    if (shard_index >= 0 && shard_index < static_cast<int>(conn_shards_.size())) {
      auto&                       shard = conn_shards_[shard_index];
      std::lock_guard<std::mutex> lock(shard->mutex);
      shard->conns.erase(handle);
    }
  }

  void Server::on_acceptor_error_(int err) {
    if (err == ECANCELED || err == EBADF) { return; }

    if (on_error_func_ != nullptr) {
      on_error_func_(err);
    }
  }

  void Server::on_poll_error_(IOEventPoll* event_poll, int err) {
    (void)event_poll;

    if (on_error_func_ != nullptr) {
      on_error_func_(err);
    }
  }

  void Server::on_new_connection_(int handle, struct sockaddr_storage addr_storage) {
    if (handle == invalid_socket) { return; }

    if (ACQUIRE_LOAD(state_) != State::kRunning) {
      Platform::close_handle(handle);
      return;
    }

    if (max_connections_ > 0 && connection_count() >= max_connections_) {
      Platform::close_handle(handle);
      if (on_error_func_ != nullptr) {
        on_error_func_(EMFILE);
      }

      return;
    }

    char     client_ip_str[INET6_ADDRSTRLEN] = {0};
    uint16_t client_port                     = 0;

    if (addr_storage.ss_family == AF_INET) {
      sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(&addr_storage);
      inet_ntop(AF_INET, &sin->sin_addr, client_ip_str, sizeof(client_ip_str));
      client_port = ntohs(sin->sin_port);
    } else if (addr_storage.ss_family == AF_INET6) {
      sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(&addr_storage);
      inet_ntop(AF_INET6, &sin6->sin6_addr, client_ip_str, sizeof(client_ip_str));
      client_port = ntohs(sin6->sin6_port);
    }

    if (client_port == 0 || strlen(client_ip_str) == 0) {
      Platform::close_handle(handle);
      return;
    }

    IOEventPoll* event_poll  = nullptr;
    int          shard_index = 0;
    if (running_mode_ == RunningMode::kOnePollPerThread) {
      event_poll = poll_thread_pool_->next_poll();
      CXPNET_CHECK(event_poll != nullptr, "Invalid event_poll");

      // thread_num_ 上限 24，线性查找分片下标的开销相对 accept 可忽略
      for (size_t i = 0; i < sub_polls_.size(); ++i) {
        if (sub_polls_[i].get() == event_poll) {
          shard_index = static_cast<int>(i);
          break;
        }
      }
    } else {
      event_poll = main_poll_.get();
    }

    auto conn = std::make_shared<Conn>(event_poll, handle);
    conn->set_remote_addr_(client_ip_str, client_port);
    conn->set_internal_close_callback_([this, shard_index, handle]() {
      on_conn_close_(shard_index, handle);
    });

    {
      auto&                       shard = conn_shards_[shard_index];
      std::lock_guard<std::mutex> lock(shard->mutex);
      shard->conns[handle] = conn;
    }

    auto on_conn_func = on_conn_func_;
    event_poll->run_in_poll([this, shard_index, handle, conn, on_conn_func]() {
      if (ACQUIRE_LOAD(state_) != State::kRunning) {
        on_conn_close_(shard_index, handle);
        if (conn->handle_ != invalid_socket) {
          Platform::close_handle(conn->handle_);
          conn->handle_ = invalid_socket;
        }

        return;
      }

      conn->start_();
      if (on_conn_func != nullptr) {
        on_conn_func(conn);
      }
    });
  }
} // namespace cxpnet
