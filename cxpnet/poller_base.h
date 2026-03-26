#ifndef POLLER_BASE_H
#define POLLER_BASE_H

#include "sock.h"
#include <unordered_map>
#include <vector>

namespace cxpnet {
  class IOEventPoll;
  class Channel;

  class PollerBase {
  public:
    PollerBase(IOEventPoll* owner)
        : owner_poll_(owner) {}
    virtual ~PollerBase() = default;

    virtual void shutdown()                                                = 0;
    virtual int  poll(int timeout, std::vector<Channel*>& active_channels) = 0;
    virtual void update_channel(Channel* channel)                          = 0;
    virtual void remove_channel(Channel* channel)                          = 0;

    bool has_channel(int handle) const {
      return channels_.find(handle) != channels_.end();
    }
  protected:
    IOEventPoll*                      owner_poll_ = nullptr;
    std::unordered_map<int, Channel*> channels_;
  };

} // namespace cxpnet

#endif // POLLER_BASE_H