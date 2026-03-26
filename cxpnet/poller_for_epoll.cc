#include "poller_for_epoll.h"
#include "channel.h"
#include "ensure.h"
#include "io_event_poll.h"
#include "platform_api.h"

namespace cxpnet {
  EpollPoller::EpollPoller(IOEventPoll* owner_poll)
      : PollerBase(owner_poll) {
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    events_.resize(kMaxPollEventCount);
  }

  EpollPoller::~EpollPoller() {
    if (epoll_fd_ >= 0) {
      Platform::close_handle(epoll_fd_);
    }
  }

  void EpollPoller::shutdown() {
    while (!channels_.empty()) {
      auto it = channels_.begin();
      remove_channel(it->second);
    }
  }

  int EpollPoller::poll(int timeout, std::vector<Channel*>& active_channels) {
    ENSURE(owner_poll_->is_in_poll_thread(), "Unsafe cross-thread operations");

    int n = epoll_wait(epoll_fd_, &*events_.begin(), static_cast<int>(events_.size()), timeout);
    if (n > 0) {
      fill_active_channels(n, active_channels);
      if (static_cast<size_t>(n) == events_.size()) { events_.resize(events_.size() * 2); }
    }

    return n;
  }

  void EpollPoller::update_channel(Channel* channel) {
    int  op         = 0;
    int  handle     = channel->handle();
    bool registered = channel->registered_in_poller();

    if (!registered) {
      if (channel->events() != 0) {
        op                = EPOLL_CTL_ADD;
        channels_[handle] = channel;
        channel->set_registered(true);
      }
    } else {
      if (channel->is_none_event()) {
        op = EPOLL_CTL_DEL;
      } else {
        op = EPOLL_CTL_MOD;
      }
    }

    if (op != 0) {
      update(op, channel);
    }

    if (op == EPOLL_CTL_DEL) {
      channels_.erase(handle);
      channel->set_registered(false);
    }
  }

  void EpollPoller::remove_channel(Channel* channel) {
    if (!channel->registered_in_poller()) {
      return;
    }

    int handle = channel->handle();
    ENSURE(has_channel(handle), "{} not in channels_", handle);
    ENSURE(channels_[handle] == channel, "Duplicate channel");

    channels_.erase(handle);
    channel->set_registered(false);
  }

  void EpollPoller::update(int op, Channel* channel) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events   = to_epoll_events(channel->events()) | EPOLLET;
    event.data.ptr = channel;

    // EPOLL_CTL_DEL 时 fd 可能已经关闭，忽略错误
    // EPOLL_CTL_MOD/ADD 时 fd 应该有效
    if (epoll_ctl(epoll_fd_, op, channel->handle(), &event) < 0) {
      if (op == EPOLL_CTL_DEL) {
        return;
      }

      ENSURE(false, "epoll_ctl failed for op={}, fd={}, errno={}", op, channel->handle(), errno);
    }
  }

  void EpollPoller::fill_active_channels(int event_n, std::vector<Channel*>& active_channels) {
    for (int i = 0; i < event_n; ++i) {
      Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
      if (!has_channel(channel->handle())) {
        continue;
      }

      int revents = from_epoll_events(events_[i].events);
      channel->set_result_events(revents);
      active_channels.push_back(channel);
    }
  }

  int EpollPoller::to_epoll_events(int events) {
    int result = 0;
    if (events & cxpnet::events::kRead) {
      result |= EPOLLIN | EPOLLRDHUP;
    }

    if (events & cxpnet::events::kWrite) {
      result |= EPOLLOUT;
    }

    return result;
  }

  int EpollPoller::from_epoll_events(uint32_t events) {
    int result = 0;
    if (events & EPOLLIN) {
      result |= cxpnet::events::kRead;
    }

    if (events & EPOLLOUT) {
      result |= cxpnet::events::kWrite;
    }

    if (events & EPOLLERR) {
      result |= cxpnet::events::kError;
    }

    if (events & EPOLLHUP) {
      result |= cxpnet::events::kHup;
    }

    if (events & EPOLLRDHUP) {
      result |= cxpnet::events::kHup;
    }

    return result;
  }
} // namespace cxpnet
