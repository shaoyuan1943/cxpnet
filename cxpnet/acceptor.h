#ifndef ACCEPTOR_H
#define ACCEPTOR_H

#include "sock.h"

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
    void set_connection_callback(std::function<void(int, struct sockaddr_storage)> func) { on_conn_func_ = std::move(func); }
    void set_acceptor_error_callback(std::function<void(int)> func) { on_err_func_ = std::move(func); }
  private:
    void _handle_read();
  private:
    using HandlesListType           = std::vector<std::pair<int, struct sockaddr_storage>>;
    using NewConnectionCallbackType = std::function<void(int, struct sockaddr_storage)>;
    int                       listen_handle_;
    IOEventPoll*              event_poll_;
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