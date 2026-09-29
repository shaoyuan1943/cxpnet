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

    // 清空本 poller 的 channel 注册表。
    // 注意：IOEventPoll::shutdown() 目前不调用它，注册表在实际运行中靠每条 Conn
    // 在自己的关闭路径上 unregister_channel() 逐条摘除，正常流程结束时它本就是空的。
    // 保留这个接口是为了让 poller 在需要时具备批量释放能力，不要假设它一定会被调用。
    virtual void shutdown()                                                = 0;
    virtual int  poll(int timeout, std::vector<Channel*>& active_channels) = 0;
    virtual void update_channel(Channel* channel)                          = 0;
    virtual void unregister_channel(Channel* channel)                      = 0;

    bool has_channel(socket_t handle) const {
      return channels_.find(handle) != channels_.end();
    }
  protected:
    IOEventPoll*                      owner_poll_ = nullptr;
    std::unordered_map<socket_t, Channel*> channels_;
  };

} // namespace cxpnet

#endif // POLLER_BASE_H
