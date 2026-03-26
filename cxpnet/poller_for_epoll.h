#ifndef POLLER_FOR_EPOLL_H
#define POLLER_FOR_EPOLL_H

#include "poller_base.h"
#include "sock.h"

#include <sys/epoll.h>

namespace cxpnet {
  class IOEventPoll;
  class Channel;

  class EpollPoller : public PollerBase {
  public:
    EpollPoller(IOEventPoll* owner_poll);
    ~EpollPoller();

    void shutdown() override;
    int  poll(int timeout, std::vector<Channel*>& active_channels) override;
    void update_channel(Channel* channel) override;
    void remove_channel(Channel* channel) override;
  private:
    void update(int op, Channel* channel);
    void fill_active_channels(int event_n, std::vector<Channel*>& active_channels);

    // 统一事件 → epoll 事件
    int to_epoll_events(int events);

    // epoll 事件 → 统一事件
    int from_epoll_events(uint32_t events);
  private:
    int                             epoll_fd_ = -1;
    std::vector<struct epoll_event> events_;
  };

} // namespace cxpnet

#endif // POLLER_FOR_EPOLL_H