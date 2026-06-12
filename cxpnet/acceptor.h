#ifndef ACCEPTOR_H
#define ACCEPTOR_H

#include "sock.h"

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace cxpnet {
  class IOEventPoll;
  class Channel;

  class Acceptor : public NonCopyable {
  private:
    enum class State {
      kCreated,
      kListening,
      kClosed,
    };
  public:
    explicit Acceptor(IOEventPoll* event_poll);
    ~Acceptor();

    void set_listen_addr(const char* addr, uint16_t port,
                         ProtocolStack proto_stack = ProtocolStack::kIPv4Only,
                         int           option      = SocketOption::kNone);

    bool listen();
    void close();
    bool is_listen() const {
      return get_state_() == State::kListening;
    }
    void set_new_conn_callback(std::function<void(int, struct sockaddr_storage)>&& func) {
      on_conn_func_ = std::move(func);
    }
    void set_error_callback(std::function<void(int)>&& func) {
      on_err_func_ = std::move(func);
    }
  private:
    void  close_local_();
    void  close_in_poll_();
    void  handle_read_();
    void  set_state_(State s) { RELEASE_STORE(state_, s); }
    State get_state_() const { return ACQUIRE_LOAD(state_); }
  private:
    using HandlesListType           = std::vector<std::pair<int, struct sockaddr_storage>>;
    using NewConnectionCallbackType = std::function<void(int, struct sockaddr_storage)>;

    IOEventPoll*              event_poll_;
    int                       listen_handle_ {invalid_socket};
    std::unique_ptr<Channel>  channel_ {nullptr};
    std::atomic<State>        state_ {State::kCreated};
    int                       sock_option_ {SocketOption::kNone};
    ProtocolStack             proto_stack_ {ProtocolStack::kIPv4Only};
    std::function<void(int)>  on_err_func_ {nullptr};
    NewConnectionCallbackType on_conn_func_ {nullptr};
    sockaddr_storage          local_addr_storage_ {};
    HandlesListType           accepted_handles_;
  };
} // namespace cxpnet

#endif // ACCEPTOR_H
