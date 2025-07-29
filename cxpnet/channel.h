#ifndef CHANNEL_H
#define CHANNEL_H

#include "base_types_value.h"
#include "platform_api.h"

namespace cxpnet {
  class IOEventPoll;
  class Channel {
  public:
    Channel(IOEventPoll* event_poll, int handle) {
      event_poll_ = event_poll;
      handle_     = handle;
    }
    ~Channel() {}

    void remove();
    void add_read_event();
    void add_write_event();
    void remove_write_event();
    void clear_event();
    void handle_event();
    void tie(const std::shared_ptr<void>& ptr);

    bool         registered_in_poller() { return registered_; }
    void         set_registered(bool registered) { registered_ = registered; }
    int          handle() const { return handle_; }
    int          events() { return events_; }
    void         set_result_events(int events) { result_events_ = events; }
    bool         reading() { return events_ & Platform::events::kRead; }
    bool         writing() { return events_ & Platform::events::kWrite; }
    IOEventPoll* event_poll() { return event_poll_; }
    bool         is_none_event() { return events_ == Platform::events::kNone; }

    void set_read_callback(std::function<void()> read_func) { on_read_func_ = std::move(read_func); }
    void set_write_callback(std::function<void()> write_func) { on_write_func_ = std::move(write_func); }
    void set_close_callback(std::function<void(int)> close_func) { on_close_func_ = std::move(close_func); }
  private:
    void _update();
    void _handle_event();
  private:
    IOEventPoll*             event_poll_    = nullptr;
    int                      handle_        = -1;
    int                      events_        = 0;
    int                      result_events_ = 0;
    int                      state_         = 0;
    bool                     registered_    = false;
    bool                     tied_          = false;
    std::weak_ptr<void>      tie_;
    std::function<void()>    on_read_func_  = nullptr;
    std::function<void()>    on_write_func_ = nullptr;
    std::function<void(int)> on_close_func_ = nullptr;
  };
} // namespace cxpnet

#endif // CHANNEL_H