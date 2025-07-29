#ifndef POLLER_FOR_EPOLL_H
#define POLLER_FOR_EPOLL_H

#include "base_types_value.h"
#include "platform_api.h"
#include <unordered_map>
#include <vector>

namespace cxpnet {
  class IOEventPoll;
  class Channel;
  class Poller {
  public:
    Poller(IOEventPoll* owner_poll) {
      owner_poll_ = owner_poll;
      epoll_fd_   = ::epoll_create1(EPOLL_CLOEXEC);
      events_.resize(kMaxPollEventCount);
    }

    ~Poller() { Platform::close_handle(epoll_fd_); }

    void shutdown();
    int  poll(int timeout, std::vector<Channel*>& active_channels);
    void update_channel(Channel* channel);
    void remove_channel(Channel* channel);
    void update(int op, Channel* channel);

    bool has_channel(int handle) { return channels_.find(handle) != channels_.end(); }
  private:
    void _fill_active_channels(int event_n, std::vector<Channel*>& active_channels);
  private:
    IOEventPoll* owner_poll_ = nullptr;
    int          epoll_fd_   = -1;

    std::unordered_map<int, Channel*> channels_;
    std::vector<struct epoll_event>   events_;
  };
} // namespace cxpnet

#endif // POLLER_FOR_EPOLL_H