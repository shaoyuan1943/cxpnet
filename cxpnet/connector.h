#ifndef CONNECTOR_H
#define CONNECTOR_H

#include "base_types_value.h"
#include <functional>
#include <memory>

namespace cxpnet {
  class IOEventPoll;
  class Channel;
  class Connector : public NonCopyable {
  public:
    Connector(IOEventPoll* event_poll, std::string_view addr, uint16_t port);
    ~Connector();

    void    start();
    ConnPtr start_by_sync();

    void set_conn_user_callback(std::function<void(ConnPtr)> func) { on_conn_func_ = std::move(func); }
    void set_error_user_callback(std::function<void(int)> func) { on_error_func_ = std::move(func); }
  private:
    void _start_in_poll();
    void _handle_write();
    int  _remove_and_get_handle();

    void _set_state(State s) { state_ = s; }
  private:
    IOEventPoll*                 event_poll_ = nullptr;
    std::string                  addr_       = "";
    uint16_t                     port_       = 0;
    State                        state_      = State::kDisconnected;
    std::unique_ptr<Channel>     channel_;
    std::function<void(ConnPtr)> on_conn_func_  = nullptr;
    std::function<void(int)>     on_error_func_ = nullptr;
  };
} // namespace cxpnet

#endif // CONNECTOR_H