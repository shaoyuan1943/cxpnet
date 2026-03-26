#ifndef POLLER_FOR_KQUEUE_H
#define POLLER_FOR_KQUEUE_H

#include "poller_base.h"
#include "sock.h"

// macOS 特定头文件
#include <sys/event.h>

namespace cxpnet {
  class IOEventPoll;
  class Channel;

  // Kqueue Poller 实现 (macOS)
  // 负责在 kqueue 事件和统一事件之间转换
  class KqueuePoller : public PollerBase {
  public:
    KqueuePoller(IOEventPoll* owner_poll);
    ~KqueuePoller();

    void shutdown() override;
    int  poll(int timeout, std::vector<Channel*>& active_channels) override;
    void update_channel(Channel* channel) override;
    void remove_channel(Channel* channel) override;

  private:
    void register_read(int fd);
    void register_write(int fd);
    void unregister_read(int fd);
    void unregister_write(int fd);
    void fill_active_channels(int event_n, std::vector<Channel*>& active_channels);

    // kqueue 事件 → 统一事件
    int from_kqueue_events(const struct kevent& ev);

  private:
    int                           kqueue_fd_ = -1;
    std::vector<struct kevent>    events_;
    std::unordered_map<int, int>  registered_events_; // fd → 已注册的统一事件
  };

} // namespace cxpnet

#endif // POLLER_FOR_KQUEUE_H