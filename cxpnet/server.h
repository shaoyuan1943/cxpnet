#ifndef SERVER_H
#define SERVER_H

#include "acceptor.h"
#include "io_base.h"
#include "io_event_poll.h"
#include "poll_thread_pool.h"

namespace cxpnet {
  class Server {
  public:
    Server(const char* addr, uint16_t port,
           ProtocolStack proto_stack = ProtocolStack::kIPv4Only, int option = SocketOption::kNone) {
      started_    = false;
      thread_num_ = 0;
      main_poll_  = std::make_unique<IOEventPoll>();
      acceptor_   = std::make_unique<Acceptor>(main_poll_.get(), addr, port, proto_stack, option);
      acceptor_->set_connection_callback(std::bind(&Server::_new_connection, this,
                                                   std::placeholders::_1, std::placeholders::_2));
    }
    ~Server() {}

    void set_thread_num(int n) { thread_num_ = n; }
    void set_conn_callback(OnConnCallback conn_func) { on_conn_func_ = conn_func; }

    void start(RunningMode mode) {
      if (thread_num_ <= 0 || started_) { return; }

      running_mode_ = mode;
      if (running_mode_ == RunningMode::kOnePollPerThread) {
        std::vector<IOEventPoll*> polls;
        for (auto i = 0; i < thread_num_; i++) {
          sub_polls_.push_back(std::make_unique<IOEventPoll>());
          polls.push_back(sub_polls_[i].get());
        }

        poll_thread_pool_ = std::make_unique<PollThreadPool>(polls);
        poll_thread_pool_->start();
      }

      started_ = true;
    }

    // blocking
    void run() {
      assert(running_mode_ == RunningMode::kOnePollPerThread);

      if (!started_) { return; }
      main_poll_->run();
    }

    void poll() {
      assert(running_mode_ == RunningMode::kAllOneThread);

      if (!started_) { return; }
      main_poll_->poll();
    }
  private:
    void _new_connection(int handle, struct sockaddr_storage addr_storage) {
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
      if (on_conn_func_ != nullptr) {
        on_conn_func_(conn);
      }

      conns_[handle] = conn;
    }
  private:
    std::unique_ptr<IOEventPoll>                   main_poll_;
    std::vector<std::unique_ptr<IOEventPoll>>      sub_polls_;
    std::unique_ptr<Acceptor>                      acceptor_;
    std::unique_ptr<std::thread>                   acceptor_thread_;
    int                                            thread_num_   = 0;
    OnConnCallback                                 on_conn_func_ = nullptr;
    bool                                           started_      = false;
    std::unique_ptr<PollThreadPool>                poll_thread_pool_;
    RunningMode                                    running_mode_;
    std::unordered_map<int, std::shared_ptr<Conn>> conns_;
  };
} // namespace cxpnet

#endif // SERVER_H