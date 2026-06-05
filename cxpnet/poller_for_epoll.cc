#include "poller_for_epoll.h"
#include "channel.h"
#include "check.h"
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
      unregister_channel(it->second);
    }
  }

  int EpollPoller::poll(int timeout, std::vector<Channel*>& active_channels) {
    CXPNET_CHECK(owner_poll_->is_in_poll_thread(), "Unsafe cross-thread operations");

    int n = epoll_wait(epoll_fd_, &*events_.begin(), static_cast<int>(events_.size()), timeout);
    if (n > 0) {
      fill_active_channels(n, active_channels);
      if (static_cast<size_t>(n) == events_.size()) {
        events_.resize(events_.size() * 2);
      }
    }

    return n;
  }

  void EpollPoller::update_channel(Channel* channel) {
    int  op         = 0;
    int  handle     = channel->handle();
    bool registered = has_channel(handle);

    if (!registered) {
      if (channel->events() != 0) {
        op                = EPOLL_CTL_ADD;
        channels_[handle] = channel;
      }
    } else {
      op = channel->is_none_event() ? EPOLL_CTL_DEL : EPOLL_CTL_ADD;
    }

    if (op != 0) {
      update(op, channel);
    }

    if (op == EPOLL_CTL_DEL) {
      channels_.erase(handle);
    }
  }

  void EpollPoller::unregister_channel(Channel* channel) {
    int handle = channel->handle();
    if (!has_channel(handle)) { return; }

    CXPNET_CHECK(has_channel(handle), "{} not in channels_", handle);
    CXPNET_CHECK(channels_[handle] == channel, "Duplicate channel");

    update(EPOLL_CTL_DEL, channel);
    channels_.erase(handle);
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

      CXPNET_CHECK(false, "epoll_ctl failed for op = {}, fd = {}, errno = {}",
                   op, channel->handle(), errno);
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
