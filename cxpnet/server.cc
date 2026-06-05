#include "server.h"
#include "acceptor.h"
#include "check.h"
#include "conn.h"
#include "io_event_poll.h"
#include "poll_thread_pool.h"
#include "sock.h"

#include <chrono>
#include <format>
#include <thread>

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
    shutdown();
    join_closing_thread_();
  }

  void Server::shutdown() {
    if (!start_close_(true)) { return; }

    if (running_mode_ == RunningMode::kAllOneThread) {
      all_one_thread_close_();
      return;
    }

    if (running_mode_ == RunningMode::kOnePollPerThread && is_in_managed_poll_thread_()) {
      start_close_thread_([this]() { do_close_(true); });
      return;
    }

    do_close_(true);
  }

  void Server::close() {
    if (!start_close_(false)) {
      try_finish_all_one_thread_close_();
      return;
    }

    if (running_mode_ == RunningMode::kOnePollPerThread && is_in_managed_poll_thread_()) {
      start_close_thread_([this]() { do_close_(false); });
      return;
    }

    do_close_(false);
  }

  // false  false   没启动/已完全停止
  // true   false   运行中，进入停止
  // false  true    停止中，升级到强制停止
  // true   true    异常状态，进入停止
  bool Server::start_close_(bool graceful) {
    const bool is_started_or_stopping = ACQUIRE_LOAD(started_) || ACQUIRE_LOAD(stopping_);
    if (!is_started_or_stopping) { return false; }

    bool expected = false;
    if (!stopping_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      if (!graceful) { RELEASE_STORE(force_stop_requested_, true); }
      return false;
    }

    if (!graceful) { RELEASE_STORE(force_stop_requested_, true); }
    RELEASE_STORE(started_, false);
    return true;
  }

  void Server::do_close_(bool graceful) {
    if (acceptor_) { acceptor_->shutdown(); }

    if (graceful) {
      for (auto& conn : collect_connections_(false)) {
        conn->shutdown();
      }

      if (graceful_close_timeout_ms_ > 0) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(graceful_close_timeout_ms_);
        while (std::chrono::steady_clock::now() < deadline) {
          if (connection_count() == 0) { break; }
          if (ACQUIRE_LOAD(force_stop_requested_)) { break; }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      }
    }

    for (auto& conn : collect_connections_(true)) {
      conn->close();
    }

    close_polls_();

    RELEASE_STORE(force_stop_requested_, false);
    RELEASE_STORE(stopping_, false);
  }

  std::vector<ConnPtr> Server::collect_connections_(bool clear_after_collect) {
    std::vector<ConnPtr>        conns_snapshot;
    std::lock_guard<std::mutex> lock(conns_mutex_);

    conns_snapshot.reserve(conns_.size());
    for (auto& [handle, conn] : conns_) {
      if (conn) { conns_snapshot.push_back(conn); }
    }

    if (clear_after_collect) { conns_.clear(); }
    return conns_snapshot;
  }

  void Server::all_one_thread_close_() {
    if (main_poll_ && !main_poll_->is_in_poll_thread()) {
      CXPNET_CHECK(false, "kAllOneThread shutdown must be called on the poll thread");
    }

    graceful_close_deadline_ = (graceful_close_timeout_ms_ > 0)
                                   ? (std::chrono::steady_clock::now() + std::chrono::milliseconds(graceful_close_timeout_ms_))
                                   : std::chrono::steady_clock::time_point::max();

    if (acceptor_) { acceptor_->shutdown(); }

    for (auto& conn : collect_connections_(false)) {
      conn->shutdown();
    }

    try_finish_all_one_thread_close_();
  }

  void Server::try_finish_all_one_thread_close_() {
    if (running_mode_ != RunningMode::kAllOneThread) { return; }
    if (!ACQUIRE_LOAD(stopping_)) { return; }

    if (!ACQUIRE_LOAD(force_stop_requested_) && connection_count() != 0 &&
        std::chrono::steady_clock::now() < graceful_close_deadline_) {
      return;
    }

    for (auto& conn : collect_connections_(true)) {
      conn->close();
    }

    close_polls_();
    RELEASE_STORE(force_stop_requested_, false);
    RELEASE_STORE(stopping_, false);
    RELEASE_STORE(started_, false);
  }

  bool Server::is_in_managed_poll_thread_() const {
    if (main_poll_ && main_poll_->is_in_poll_thread()) { return true; }

    for (const auto& poll : sub_polls_) {
      if (poll && poll->is_in_poll_thread()) { return true; }
    }

    return false;
  }

  void Server::start_close_thread_(Closure func) {
    std::lock_guard<std::mutex> lock(closing_thread_mutex_);
    if (closing_thread_.joinable()) {
      closing_thread_.join();
    }

    closing_thread_ = std::thread(std::move(func));
  }

  void Server::join_closing_thread_() {
    std::lock_guard<std::mutex> lock(closing_thread_mutex_);
    if (!closing_thread_.joinable()) { return; }

    if (closing_thread_.get_id() == std::this_thread::get_id()) {
      closing_thread_.detach();
      return;
    }

    closing_thread_.join();
  }

  void Server::close_polls_() {
    if (poll_thread_pool_) { poll_thread_pool_->shutdown(); }
    if (main_poll_) { main_poll_->shutdown(); }
  }

  bool Server::start(RunningMode mode) {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      return false;
    }

    RELEASE_STORE(stopping_, false);
    RELEASE_STORE(force_stop_requested_, false);

    running_mode_ = mode;

    constexpr int kMaxThreadNum = 1024;
    if (running_mode_ == RunningMode::kOnePollPerThread) {
      if (thread_num_ <= 0 || thread_num_ > kMaxThreadNum) {
        RELEASE_STORE(started_, false);
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
    }

    if (!acceptor_->listen()) {
      close_polls_();
      RELEASE_STORE(started_, false);
      return false;
    }

    return true;
  }

  void Server::run() {
    CXPNET_CHECK(running_mode_ == RunningMode::kOnePollPerThread, "");

    if (!ACQUIRE_LOAD(started_)) { return; }
    main_poll_->run();
  }

  void Server::poll() {
    CXPNET_CHECK(running_mode_ == RunningMode::kAllOneThread, "");

    const bool is_started_or_stopping = ACQUIRE_LOAD(started_) || ACQUIRE_LOAD(stopping_);
    if (!is_started_or_stopping) { return; }

    main_poll_->poll();
    try_finish_all_one_thread_close_();
  }

  void Server::on_conn_close_(int handle) {
    std::lock_guard<std::mutex> lock(conns_mutex_);
    conns_.erase(handle);
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

    if (!ACQUIRE_LOAD(started_) || ACQUIRE_LOAD(stopping_)) {
      Platform::close_handle(handle);
      return;
    }

    {
      std::lock_guard<std::mutex> lock(conns_mutex_);
      if (max_connections_ > 0 && conns_.size() >= max_connections_) {
        Platform::close_handle(handle);
        if (on_error_func_ != nullptr) {
          on_error_func_(EMFILE);
        }

        return;
      }
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

    IOEventPoll* event_poll = nullptr;
    if (running_mode_ == RunningMode::kOnePollPerThread) {
      event_poll = poll_thread_pool_->next_poll();
      CXPNET_CHECK(event_poll != nullptr, "Invalid event_poll");
    } else {
      event_poll = main_poll_.get();
    }

    auto conn = std::make_shared<Conn>(event_poll, handle);
    conn->set_remote_addr_(client_ip_str, client_port);
    conn->set_internal_close_callback_([this, handle]() {
      on_conn_close_(handle);
    });

    {
      std::lock_guard<std::mutex> lock(conns_mutex_);
      conns_[handle] = conn;
    }

    auto on_conn_func = on_conn_func_;
    event_poll->run_in_poll([this, handle, conn, on_conn_func]() {
      if (!ACQUIRE_LOAD(started_) || ACQUIRE_LOAD(stopping_)) {
        on_conn_close_(handle);
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
