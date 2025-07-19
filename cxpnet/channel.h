#ifndef CHANNEL_H
#define CHANNEL_H

#include "base_type_value.h"
#include "io_event_poll.h"
#include "poller_for_epoll.h"

namespace cxpnet {
  class Channel {
  public:
    Channel(IOEventPoll* event_poll, int handle) {
      event_poll_ = event_poll;
      handle_     = handle;
    }
    ~Channel() {}

    bool         registered_in_poller() { return registered_; }
    void         set_registered(bool registered) { registered_ = registered; }
    int          handle() const { return handle_; }
    int          events() { return events_; }
    void         set_result_events(int events) { result_events_ = events; }
    bool         reading() { return events_ & platform::events::kRead; }
    bool         writing() { return events_ & platform::events::kWrite; }
    IOEventPoll* event_poll() { return event_poll_; }
    bool         is_none_event() { return events_ == platform::events::kNone; }
    void         remove() { event_poll_->remove_channel(this); }

    void add_read_event() {
      if (reading()) { return; }
      events_ |= platform::events::kRead;
      _update();
    }
    void add_write_event() {
      if (writing()) { return; }
      events_ |= platform::events::kWrite;
      _update();
    }
    void remove_write_event() {
      if (!writing()) { return; }
      events_ &= ~platform::events::kWrite;
      _update();
    }
    void clear_event() {
      events_ = 0;
      _update();
    }

    void handle_event() {
      if (tied_) {
        if (auto sp = tie_.lock()) { _handle_event(); }
        return;
      }

      _handle_event();
    }
    void tie(const std::shared_ptr<void>& ptr) {
      tie_  = ptr; // tie_ 是一个 weak_ptr，它观察 obj
      tied_ = true;
    }
    void set_read_callback(std::function<void()> read_func) { on_read_func_ = std::move(read_func); }
    void set_write_callback(std::function<void()> write_func) { on_write_func_ = std::move(write_func); }
    void set_close_callback(std::function<void(int)> close_func) { on_close_func_ = std::move(close_func); }
  private:
    void _update() { event_poll_->update_channel(this); }
    void _handle_event() {
      if (result_events_ & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        int err = 0;
        if (result_events_ & EPOLLERR) {
          socklen_t err_len = sizeof(err);
          getsockopt(handle_, SOL_SOCKET, SO_ERROR, &err, &err_len);
        }

        // try recv data befor closing
        if ((result_events_ & EPOLLIN) || (result_events_ & EPOLLHUP)) {
          if (on_read_func_ != nullptr) { on_read_func_(); }
        }

        if (on_close_func_ != nullptr) { on_close_func_(err); }
        return;
      }

      if (result_events_ & EPOLLIN) {
        if (on_read_func_ != nullptr) { on_read_func_(); }
      }

      if (result_events_ & EPOLLOUT) {
        if (on_write_func_ != nullptr) { on_write_func_(); }
      }
    }
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