#ifndef CHANNEL_H
#define CHANNEL_H

#include "platform_api.h"
#include "sock.h"

namespace cxpnet {
  class IOEventPoll;

  // Channel 负责一个 fd 的事件分发
  // 使用统一的事件常量，平台无关
  class Channel {
  public:
    Channel(IOEventPoll* event_poll, int handle);
    ~Channel() {}

    void remove();
    void add_read_event();
    void add_write_event();
    void remove_write_event();
    void clear_event();
    void handle_event();
    void tie(const std::shared_ptr<void>& ptr);

    bool         registered_in_poller() const { return registered_; }
    void         set_registered(bool registered) { registered_ = registered; }
    int          handle() const { return handle_; }
    int          events() const { return events_; }
    void         set_result_events(int events) { result_events_ = events; }
    bool         reading() const { return events_ & events::kRead; }
    bool         writing() const { return events_ & events::kWrite; }
    IOEventPoll* event_poll() const { return event_poll_; }
    bool         is_none_event() const { return events_ == events::kNone; }

    void set_read_callback(std::function<void()>&& func) { on_read_func_ = std::move(func); }
    void set_write_callback(std::function<void()>&& func) { on_write_func_ = std::move(func); }
    void set_close_callback(std::function<void(int)>&& func) { on_close_func_ = std::move(func); }
  private:
    void update_();
    void handle_event_();
  private:
    IOEventPoll*             event_poll_;
    int                      handle_;
    int                      events_;        // 统一事件
    int                      result_events_; // 统一事件
    bool                     registered_;
    bool                     tied_;
    std::weak_ptr<void>      tie_;
    std::function<void()>    on_read_func_;
    std::function<void()>    on_write_func_;
    std::function<void(int)> on_close_func_;
  };
} // namespace cxpnet

#endif // CHANNEL_H
