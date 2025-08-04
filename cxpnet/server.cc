#include "server.h"
#include "acceptor.h"
#include "base_types_value.h"
#include "conn.h"
#include "ensure.h"
#include "io_event_poll.h"
#include "poll_thread_pool.h"

namespace cxpnet {
  Server::Server(const char* addr, uint16_t port, ProtocolStack proto_stack, int option) {
    started_                = false;
    thread_num_             = 0;
    on_conn_func_           = nullptr;
    on_poll_error_func_     = nullptr;
    on_acceptor_error_func_ = nullptr;
    main_poll_              = std::make_unique<IOEventPoll>();
    acceptor_               = std::make_unique<Acceptor>(main_poll_.get(), addr, port, proto_stack, option);

    acceptor_->set_connection_callback(std::bind(&Server::_on_new_connection, this, std::placeholders::_1, std::placeholders::_2));
    acceptor_->set_acceptor_err_callback(std::bind(&Server::_on_acceptor_error, this, std::placeholders::_1));
    
    main_poll_->set_error_callback(std::bind(&Server::_on_poll_error, this, std::placeholders::_1, std::placeholders::_2));
    main_poll_->set_name("main_poll");
  }

  Server::~Server() {}
  void Server::shutdown() {
    if (acceptor_) { acceptor_->shutdown(); }
    if (poll_thread_pool_) { poll_thread_pool_->shutdown(); }
    if (main_poll_) { main_poll_->shutdown(); }

    started_ = false;
  }

  void Server::start(RunningMode mode) {
    if (thread_num_ <= 0 || started_) { return; }

    running_mode_ = mode;
    if (running_mode_ == RunningMode::kOnePollPerThread) {
      sub_polls_.reserve(thread_num_);
      std::vector<IOEventPoll*> polls;
      polls.reserve(thread_num_);
      for (auto i = 0; i < thread_num_; i++) {
        auto poll = std::make_unique<IOEventPoll>();
        poll->set_name(std::format("sub_poll_{}", i + 1));
        poll->set_error_callback(std::bind(&Server::_on_poll_error, this, std::placeholders::_1, std::placeholders::_2));
        polls.push_back(poll.get());
        
        sub_polls_.push_back(std::move(poll));
      }

      poll_thread_pool_ = std::make_unique<PollThreadPool>(polls);
      poll_thread_pool_->start();
    }

    acceptor_->listen();
    started_ = true;
  }

  // blocking
  void Server::run() {
    assert(running_mode_ == RunningMode::kOnePollPerThread);

    if (!started_) { return; }
    main_poll_->run();
  }

  void Server::poll() {
    assert(running_mode_ == RunningMode::kAllOneThread);

    if (!started_) { return; }
    main_poll_->poll();
  }

  void Server::_on_conn_close(int handle) {
    main_poll_->run_in_poll([this, handle]() {
      ENSURE(conns_.find(handle) != conns_.end(), "{} not in conns_", handle);
      conns_.erase(handle);
    });
  }

  void Server::_on_acceptor_error(int err) {
    if (on_acceptor_error_func_ != nullptr) {
      on_acceptor_error_func_(err);
    }
  }

  void Server::_on_poll_error(IOEventPoll* event_poll, int err) {
    if (on_poll_error_func_ != nullptr) {
      on_poll_error_func_(event_poll, err);
    }
  }

  void Server::_on_new_connection(int handle, struct sockaddr_storage addr_storage) {
    if (handle == invalid_socket) { return; }
    char     client_ip_str[INET6_ADDRSTRLEN] = {0};
    uint16_t client_port                     = 0;

    if (addr_storage.ss_family == AF_INET) {
      sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(&addr_storage);
      inet_ntop(AF_INET, &sin->sin_addr, client_ip_str, sizeof(client_ip_str));
      client_port = ntohs(sin->sin_port);
    }

    if (addr_storage.ss_family == AF_INET6) {
      sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(&addr_storage);
      inet_ntop(AF_INET6, &sin6->sin6_addr, client_ip_str, sizeof(client_ip_str));
      client_port = ntohs(sin6->sin6_port);
    }

    if (client_port == 0 || strlen(client_ip_str) == 0) { return; }

    IOEventPoll* event_poll = nullptr;
    if (running_mode_ == RunningMode::kOnePollPerThread) {
      event_poll = poll_thread_pool_->next_poll();
      assert(event_poll != nullptr);
    }

    if (running_mode_ == RunningMode::kAllOneThread) {
      event_poll = main_poll_.get();
    }

    auto conn = std::make_shared<Conn>(event_poll, handle);
    conn->set_remote_addr(client_ip_str, client_port);
    conn->_set_on_close_holder_func([this, handle]() {
      _on_conn_close(handle);
    });

    conns_[handle] = conn;
    conn->_start();

    if (on_conn_func_ != nullptr) {
      on_conn_func_(conn);
    }
  }

} // namespace cxpnet
