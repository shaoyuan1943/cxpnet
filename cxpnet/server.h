#ifndef SERVER_H
#define SERVER_H

#include "base_types_value.h"
#include <vector>
#include <thread>
#include <memory>

namespace cxpnet {
  class Acceptor;
  class Conn;
  class IOEventPoll;
  class PollThreadPool;
  class Server : public NonCopyable {
  public:
    Server(const char* addr, uint16_t port,
           ProtocolStack proto_stack = ProtocolStack::kIPv4Only, int option = SocketOption::kNone);
    ~Server();

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    void shutdown();
    void start(RunningMode mode);
    void run();
    void poll();

    void set_thread_num(int n) { thread_num_ = n; }
    void set_conn_user_callback(OnConnectionCallback conn_func) {
      on_conn_func_ = std::move(conn_func);
    }
    void set_poll_error_user_callback(OnEventPollErrorCallback err_func) {
      on_poll_error_func_ = std::move(err_func);
    }
  private:
    void _remove_conn(int handle);
    void _on_acceptor_error(int err);
    void _on_poll_error(IOEventPoll* event_poll, int err);
    void _on_new_connection(int handle, struct sockaddr_storage addr_storage);
  private:
    std::unique_ptr<IOEventPoll>                   main_poll_;
    std::vector<std::unique_ptr<IOEventPoll>>      sub_polls_;
    std::unique_ptr<Acceptor>                      acceptor_;
    std::unique_ptr<std::thread>                   acceptor_thread_;
    int                                            thread_num_         = 0;
    OnConnectionCallback                           on_conn_func_       = nullptr;
    OnEventPollErrorCallback                       on_poll_error_func_ = nullptr;
    bool                                           started_            = false;
    std::unique_ptr<PollThreadPool>                poll_thread_pool_;
    RunningMode                                    running_mode_;
    std::unordered_map<int, std::shared_ptr<Conn>> conns_;
  };
} // namespace cxpnet

#endif // SERVER_H