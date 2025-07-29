#ifndef ACCEPTOR_H
#define ACCEPTOR_H

#include "base_types_value.h"

namespace cxpnet {
  class IOEventPoll;
  class Channel;
  class Acceptor : public NonCopyable {
  public:
    Acceptor(IOEventPoll* event_poll, const char* addr, uint16_t port,
             ProtocolStack proto_stack = ProtocolStack::kIPv4Only, int option = SocketOption::kNone);
    ~Acceptor();
    void shutdown();
    bool listen();

    bool is_listen() { return listening_; }
    void set_connection_callback(std::function<void(int, struct sockaddr_storage)> conn_func) { on_conn_func_ = std::move(conn_func); }
    void set_acceptor_err_callback(std::function<void(int)> err_func) { on_err_func_ = std::move(err_func); }
  private:
    void _handle_read();
  private:
    using HandlesListType                    = std::vector<std::pair<int, struct sockaddr_storage>>;
    using NewConnectionCallbackType          = std::function<void(int, struct sockaddr_storage)>;
    int                       listen_handle_ = -1;
    IOEventPoll*              event_poll_    = nullptr;
    std::unique_ptr<Channel>  channel_;
    bool                      listening_    = false;
    int                       sock_option_  = SocketOption::kNone;
    ProtocolStack             proto_stack_  = ProtocolStack::kIPv4Only;
    std::function<void(int)>  on_err_func_  = nullptr;
    NewConnectionCallbackType on_conn_func_ = nullptr;
    struct sockaddr_storage   local_addr_storage_;
    HandlesListType           accepted_handles_;
  };
} // namespace cxpnet

#endif // ACCEPTOR_H