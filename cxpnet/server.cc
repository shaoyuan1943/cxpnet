#include "server.h"
#include "acceptor.h"
#include "conn.h"
#include "ensure.h"
#include "io_event_poll.h"
#include "poll_thread_pool.h"
#include "sock.h"

#include <chrono>
#include <format>
#include <future>

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
    join_exit_thread_();
  }

  void Server::shutdown() {
    if (!started_.load(std::memory_order_acquire) &&
        !shutting_down_.load(std::memory_order_acquire)) {
      return;
    }

    if (running_mode_ == RunningMode::kAllOneThread) {
      shutdown_all_one_thread_();
      return;
    }

    if (!started_.exchange(false, std::memory_order_acq_rel)) { return; }

    if (is_in_managed_poll_thread_()) {
      start_exit_thread_([this]() {
        shutdown_impl_();
      });
      return;
    }

    shutdown_impl_();
  }

  void Server::shutdown_impl_() {
    if (acceptor_) { acceptor_->shutdown(); }

    std::vector<std::shared_ptr<Conn>> conns_snapshot;
    {
      std::lock_guard<std::mutex> lock(conns_mutex_);
      conns_snapshot.reserve(conns_.size());
      for (auto& [handle, conn] : conns_) {
        if (conn) { conns_snapshot.push_back(conn); }
      }
    }

    for (auto& conn : conns_snapshot) {
      run_in_poll_and_wait_(conn->event_poll_, [conn]() {
        conn->shutdown_in_poll_();
      });
    }

    if (shutdown_timeout_ms_ > 0) {
      auto deadline = std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(shutdown_timeout_ms_);
      while (std::chrono::steady_clock::now() < deadline) {
        if (connection_count() == 0) { break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }

    conns_snapshot.clear();
    {
      std::lock_guard<std::mutex> lock(conns_mutex_);
      conns_snapshot.reserve(conns_.size());
      for (auto& [handle, conn] : conns_) {
        if (conn) { conns_snapshot.push_back(conn); }
      }
      conns_.clear();
    }

    for (auto& conn : conns_snapshot) {
      run_in_poll_and_wait_(conn->event_poll_, [conn]() {
        conn->cleanup_(0);
      });
    }

    shutdown_polls_();
    shutting_down_.store(false, std::memory_order_release);
  }

  void Server::close() {
    bool was_started = started_.exchange(false, std::memory_order_acq_rel);
    if (!was_started && !shutting_down_.exchange(false, std::memory_order_acq_rel)) { return; }

    if (running_mode_ == RunningMode::kOnePollPerThread && is_in_managed_poll_thread_()) {
      start_exit_thread_([this]() {
        close_impl_();
      });
      return;
    }

    close_impl_();
  }

  void Server::shutdown_all_one_thread_() {
    if (!started_.load(std::memory_order_acquire)) { return; }
    if (shutting_down_.exchange(true, std::memory_order_acq_rel)) { return; }

    if (main_poll_ && !main_poll_->is_in_poll_thread()) {
      ENSURE(false, "kAllOneThread shutdown must be called on the poll thread");
    }

    shutdown_deadline_ = (shutdown_timeout_ms_ > 0)
                             ? (std::chrono::steady_clock::now() + std::chrono::milliseconds(shutdown_timeout_ms_))
                             : std::chrono::steady_clock::time_point::max();

    if (acceptor_) { acceptor_->shutdown(); }

    std::vector<std::shared_ptr<Conn>> conns_snapshot;
    {
      std::lock_guard<std::mutex> lock(conns_mutex_);
      conns_snapshot.reserve(conns_.size());
      for (auto& [handle, conn] : conns_) {
        if (conn) { conns_snapshot.push_back(conn); }
      }
    }

    for (auto& conn : conns_snapshot) {
      conn->shutdown_in_poll_();
    }

    finish_shutdown_all_one_thread_();
  }

  void Server::finish_shutdown_all_one_thread_() {
    if (running_mode_ != RunningMode::kAllOneThread) { return; }
    if (!shutting_down_.load(std::memory_order_acquire)) { return; }

    if (connection_count() != 0 &&
        std::chrono::steady_clock::now() < shutdown_deadline_) {
      return;
    }

    std::vector<std::shared_ptr<Conn>> conns_snapshot;
    {
      std::lock_guard<std::mutex> lock(conns_mutex_);
      conns_snapshot.reserve(conns_.size());
      for (auto& [handle, conn] : conns_) {
        if (conn) { conns_snapshot.push_back(conn); }
      }
      conns_.clear();
    }

    for (auto& conn : conns_snapshot) {
      conn->cleanup_(0);
    }

    shutdown_polls_();
    shutting_down_.store(false, std::memory_order_release);
    started_.store(false, std::memory_order_release);
  }

  void Server::close_impl_() {
    if (acceptor_) { acceptor_->shutdown(); }

    std::vector<std::shared_ptr<Conn>> conns_snapshot;
    {
      std::lock_guard<std::mutex> lock(conns_mutex_);
      conns_snapshot.reserve(conns_.size());
      for (auto& [handle, conn] : conns_) {
        if (conn) { conns_snapshot.push_back(conn); }
      }
      conns_.clear();
    }

    for (auto& conn : conns_snapshot) {
      run_in_poll_and_wait_(conn->event_poll_, [conn]() {
        conn->cleanup_(0);
      });
    }

    shutdown_polls_();
    shutting_down_.store(false, std::memory_order_release);
  }

  void Server::run_in_poll_and_wait_(IOEventPoll* event_poll, Closure func) {
    if (event_poll == nullptr) { return; }

    if (event_poll->is_in_poll_thread()) {
      func();
      return;
    }

    auto done   = std::make_shared<std::promise<void>>();
    auto future = done->get_future();

    event_poll->run_in_poll([func = std::move(func), done]() mutable {
      func();
      done->set_value();
    });

    future.get();
  }

  bool Server::is_in_managed_poll_thread_() const {
    if (main_poll_ && main_poll_->is_in_poll_thread()) { return true; }

    for (const auto& poll : sub_polls_) {
      if (poll && poll->is_in_poll_thread()) { return true; }
    }

    return false;
  }

  void Server::start_exit_thread_(Closure func) {
    std::lock_guard<std::mutex> lock(exit_thread_mutex_);
    if (exit_thread_.joinable()) {
      exit_thread_.join();
    }

    exit_thread_ = std::thread(std::move(func));
  }

  void Server::join_exit_thread_() {
    std::lock_guard<std::mutex> lock(exit_thread_mutex_);
    if (!exit_thread_.joinable()) { return; }

    if (exit_thread_.get_id() == std::this_thread::get_id()) {
      exit_thread_.detach();
      return;
    }

    exit_thread_.join();
  }

  void Server::shutdown_polls_() {
    if (poll_thread_pool_) { poll_thread_pool_->shutdown(); }
    if (main_poll_) { main_poll_->shutdown(); }
  }

  bool Server::start(RunningMode mode) {
    if (thread_num_ <= 0) { return false; }

    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      return false;
    }

    constexpr int kMaxThreadNum = 1024;
    if (thread_num_ > kMaxThreadNum) {
      started_.store(false, std::memory_order_release);
      return false;
    }

    running_mode_ = mode;
    if (running_mode_ == RunningMode::kOnePollPerThread) {
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

      poll_thread_pool_ = std::make_unique<PollThreadPool>(polls);
      poll_thread_pool_->start();
    }

    if (!acceptor_->listen()) {
      shutdown_polls_();
      started_.store(false, std::memory_order_release);
      return false;
    }
    return true;
  }

  void Server::run() {
    assert(running_mode_ == RunningMode::kOnePollPerThread);

    if (!started_.load(std::memory_order_acquire)) { return; }
    main_poll_->run();
  }

  void Server::poll() {
    assert(running_mode_ == RunningMode::kAllOneThread);

    if (!started_.load(std::memory_order_acquire) &&
        !shutting_down_.load(std::memory_order_acquire)) {
      return;
    }
    main_poll_->poll();
    finish_shutdown_all_one_thread_();
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
      assert(event_poll != nullptr);
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
    event_poll->run_in_poll([conn, on_conn_func]() {
      conn->start_();
      if (on_conn_func != nullptr) {
        on_conn_func(conn);
      }
    });
  }
} // namespace cxpnet
