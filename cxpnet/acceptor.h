#ifndef ACCEPTOR_H
#define ACCEPTOR_H

#include "sock.h"

#include <functional>
#include <memory>
#include <vector>

namespace cxpnet {
  class IOEventPoll;
  class Channel;

  class Acceptor : public NonCopyable {
  public:
    explicit Acceptor(IOEventPoll* event_poll);
    ~Acceptor();

    void set_listen_addr(const char* addr, uint16_t port,
                         ProtocolStack proto_stack = ProtocolStack::kIPv4Only,
                         int           option      = SocketOption::kNone);

    bool listen();
    void shutdown();

    bool is_listen() const { return listening_; }

    void set_new_conn_callback(std::function<void(int, struct sockaddr_storage)>&& func) {
      on_conn_func_ = std::move(func);
    }
    void set_error_callback(std::function<void(int)>&& func) {
      on_err_func_ = std::move(func);
    }
  private:
    void shutdown_local_();
    void shutdown_in_poll_();
    void handle_read_();
  private:
    using HandlesListType           = std::vector<std::pair<int, struct sockaddr_storage>>;
    using NewConnectionCallbackType = std::function<void(int, struct sockaddr_storage)>;

    IOEventPoll*              event_poll_;
    int                       listen_handle_;
    std::unique_ptr<Channel>  channel_;
    bool                      listening_;
    int                       sock_option_;
    ProtocolStack             proto_stack_;
    std::function<void(int)>  on_err_func_;
    NewConnectionCallbackType on_conn_func_;
    sockaddr_storage          local_addr_storage_;
    HandlesListType           accepted_handles_;
  };
} // namespace cxpnet

#endif // ACCEPTOR_H
