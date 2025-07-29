#include "channel.h"
#include "io_event_poll.h"

namespace cxpnet {
  void Channel::add_read_event() {
    if (reading()) { return; }
    events_ |= Platform::events::kRead;
    _update();
  }

  void Channel::add_write_event() {
    if (writing()) { return; }
    events_ |= Platform::events::kWrite;
    _update();
  }

  void Channel::remove_write_event() {
    if (!writing()) { return; }
    events_ &= ~Platform::events::kWrite;
    _update();
  }

  void Channel::clear_event() {
    events_ = 0;
    _update();
  }

  void Channel::handle_event() {
    if (tied_) {
      if (auto sp = tie_.lock()) { _handle_event(); }
      return;
    }

    _handle_event();
  }

  void Channel::tie(const std::shared_ptr<void>& ptr) {
    tie_  = ptr;
    tied_ = true;
  }

  void Channel::remove() { event_poll_->remove_channel(this); }

  void Channel::_update() { event_poll_->update_channel(this); }

  void Channel::_handle_event() {
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
} // namespace cxpnet