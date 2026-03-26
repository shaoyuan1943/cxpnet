#include "channel.h"
#include "ensure.h"
#include "io_event_poll.h"
#include "platform_api.h"

namespace cxpnet {
  Channel::Channel(IOEventPoll* event_poll, int handle)
      : event_poll_ {event_poll}
      , handle_ {handle}
      , events_ {0}
      , result_events_ {0}
      , registered_ {false}
      , tied_ {false}
      , on_read_func_ {nullptr}
      , on_write_func_ {nullptr}
      , on_close_func_ {nullptr} {
  }

  void Channel::add_read_event() {
    if (reading()) { return; }
    events_ |= events::kRead;
    update_();
  }

  void Channel::add_write_event() {
    if (writing()) { return; }
    events_ |= events::kWrite;
    update_();
  }

  void Channel::remove_write_event() {
    if (!writing()) { return; }
    events_ &= ~events::kWrite;
    update_();
  }

  void Channel::clear_event() {
    events_ = 0;
    update_();
  }

  void Channel::handle_event() {
    if (tied_) {
      if (auto sp = tie_.lock()) { handle_event_(); }
      return;
    }

    handle_event_();
  }

  void Channel::tie(const std::shared_ptr<void>& ptr) {
    tie_  = ptr;
    tied_ = true;
  }

  void Channel::remove() {
    event_poll_->remove_channel(this);
  }

  void Channel::update_() {
    event_poll_->update_channel(this);
  }

  void Channel::handle_event_() {
    if (result_events_ & (events::kError | events::kHup)) {
      int err = 0;
      if (result_events_ & events::kError) {
        socklen_t err_len = sizeof(err);
        getsockopt(handle_, SOL_SOCKET, SO_ERROR, &err, &err_len);
      }

      // try recv data before closing
      if (result_events_ & events::kRead) {
        if (on_read_func_ != nullptr) { on_read_func_(); }
      }

      if (on_close_func_ != nullptr) {
        on_close_func_(err);
      }

      return;
    }

    if (result_events_ & events::kRead) {
      if (on_read_func_ != nullptr) { on_read_func_(); }
    }

    if (result_events_ & events::kWrite) {
      if (on_write_func_ != nullptr) { on_write_func_(); }
    }
  }
} // namespace cxpnet