#ifndef IO_POLL_H
#define IO_POLL_H

#include "platform_api.h"
#include "poller_base.h"
#include "sock.h"
#include "timer.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace cxpnet {
  class Channel;
  class Conn;

  class IOEventPoll : public NonCopyable {
  public:
    IOEventPoll();
    ~IOEventPoll();

    void poll(); // non-blocking
    void run();  // blocking
    void shutdown();
    void run_in_poll(Closure func);
    void run_later(Closure func);
    void update_channel(Channel* channel);
    void remove_channel(Channel* channel);

    void             set_name(std::string_view name) { name_ = name; }
    std::string_view name() const { return name_; }
    bool             is_in_poll_thread() const { return thread_id_ == std::this_thread::get_id(); }
    bool             is_shutdown() const { return shut_.load(std::memory_order_acquire); }
    void             set_error_callback(std::function<void(IOEventPoll*, int)>&& func) { on_err_func_ = std::move(func); }
    TimerManager*    timer_manager() const { return timer_manager_.get(); }
  private:
    void notify_wakeup_();
    void handle_wakeup_();
    void poll_(uint32_t poll_timeout);
  private:
    std::unique_ptr<PollerBase>            poller_;
    std::unique_ptr<TimerManager>          timer_manager_;
    int                                    wakeup_handle_  = -1;
    int                                    wakeup_read_fd_ = -1; // for macos
    std::unique_ptr<Channel>               wakeup_channel_;
    std::vector<Closure>                   tasks_;
    std::mutex                             mutex_;
    std::thread::id                        thread_id_;
    std::vector<Channel*>                  active_channels_;
    std::atomic<bool>                      shut_ {false};
    std::function<void(IOEventPoll*, int)> on_err_func_;
    std::string                            name_;
  };
} // namespace cxpnet

#endif // IO_POLL_H
