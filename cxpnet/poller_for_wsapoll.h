#ifndef CXPNET_POLLER_FOR_WSAPOLL_H
#define CXPNET_POLLER_FOR_WSAPOLL_H

#include "poller_base.h"

namespace cxpnet {
  class WSAPollPoller : public PollerBase {
  public:
    explicit WSAPollPoller(IOEventPoll* owner_poll)
        : PollerBase(owner_poll) {}
    void shutdown() override;
    int  poll(int timeout, std::vector<Channel*>& active_channels) override;
    void update_channel(Channel* channel) override;
    void unregister_channel(Channel* channel) override;
  private:
    std::vector<WSAPOLLFD> poll_fds_;
  };
} // namespace cxpnet

#endif // CXPNET_POLLER_FOR_WSAPOLL_H
