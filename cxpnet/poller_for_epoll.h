#ifndef POLLER_FOR_EPOLL_H
#define POLLER_FOR_EPOLL_H

#include "base_type_value.h"
#include "channel.h"
#include "ensure.h"
#include "io_event_poll.h"
#include <unordered_map>

namespace cxpnet {
  class Poller {
  public:
    Poller(IOEventPoll* owner_poll) {
      owner_poll_ = owner_poll;
      epoll_fd_   = ::epoll_create1(EPOLL_CLOEXEC);
      events_.resize(kMaxPollEventCount);
    }

    ~Poller() {
      platform::close_handle(epoll_fd_);
    }

    void shutdown() {
      for (auto&& kv : channels_) {
        remove_channel(kv.second);
      }

      channels_.clear();
    }

    int poll(int timeout, std::vector<Channel*>& active_channels) {
      ENSURE(owner_poll_->is_in_poll_thread(), "Unsafe cross-thread operations");

      int n = epoll_wait(epoll_fd_, &*events_.begin(), static_cast<int>(events_.size()), timeout);
      if (n > 0) {
        _fill_active_channels(n, active_channels);
        if (static_cast<size_t>(n) == events_.size()) { events_.resize(events_.size() * 2); }
      }

      return n;
    }

    void update_channel(Channel* channel) {
      int  op         = 0;
      int  handle     = channel->handle();
      bool registered = channel->registered_in_poller();
      if (!registered && channel->events() != 0) { op = EPOLL_CTL_ADD; }

      if (registered) {
        if (channel->is_none_event()) {
          op = EPOLL_CTL_DEL;
          channels_.erase(handle);
        } else {
          op = EPOLL_CTL_MOD;
        }
      }

      if (op == EPOLL_CTL_ADD) { 
        channels_[handle] = channel;
        channel->set_registered(true); 
      }
      ENSURE(op != 0, "EPOLL_CTL failed");

      update(op, channel);
    }

    void remove_channel(Channel* channel) {
      int handle = channel->handle();
      ENSURE(has_channel(handle), "{} not in channels_", handle);
      ENSURE(channels_[handle] == channel, "Duplicate channel");
      ENSURE(channel->is_none_event(), "Must invoke 'clear_event' first");

      channels_.erase(handle);
      channel->set_registered(false);
    }

    bool has_channel(int handle) { return channels_.find(handle) != channels_.end(); }
    void update(int op, Channel* channel) {
      struct epoll_event event;
      memset(&event, 0, sizeof(event));
      event.events   = channel->events() | EPOLLET;
      event.data.ptr = channel;
      if (epoll_ctl(epoll_fd_, op, channel->handle(), &event) < 0) {
        ENSURE(false, "epoll_ctl failed");
      }
    }
  private:
    void _fill_active_channels(int event_n, std::vector<Channel*>& active_channels) {
      for (int i = 0; i < event_n; ++i) {
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
        ENSURE(has_channel(channel->handle()), "Not found channel in epoll {}", channel->handle());

        channel->set_result_events(events_[i].events);
        active_channels.push_back(channel);
      }
    }
  private:
    IOEventPoll* owner_poll_ = nullptr;
    int          epoll_fd_   = -1;

    std::unordered_map<int, Channel*> channels_;
    std::vector<struct epoll_event>   events_;
  };
} // namespace cxpnet

#endif // POLLER_FOR_EPOLL_H