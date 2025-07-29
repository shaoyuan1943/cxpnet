#ifndef IO_POLL_H
#define IO_POLL_H

#include "base_types_value.h"
#include "platform_api.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace cxpnet {
  class Channel;
  class IOEventPoll : public NonCopyable {
  public:
    IOEventPoll();
    ~IOEventPoll();

    void poll(); // non-blocking
    void run();  // blocking
    void shutdown();
    void run_in_poll(Closure func);
    void update_channel(Channel* channel);
    void remove_channel(Channel* channel);

    void             set_name(std::string_view name) { name_ = name; }
    std::string_view name() { return name_; }
    bool             is_in_poll_thread() { return thread_id_ == std::this_thread::get_id(); }
    void             set_error_callback(OnEventPollErrorCallback err_func) { on_err_func_ = std::move(err_func); }
  private:
    void _notify_wakeup() { Platform::write_to_fd(wakeup_handle_); }
    void _handle_wakeup() { Platform::read_from_fd(wakeup_handle_); }

    void _poll(uint32_t poll_timeout);
  private:
    std::unique_ptr<Poller>  poller_;
    int                      wakeup_handle_ = -1;
    std::unique_ptr<Channel> wakeup_channel_;
    std::vector<Closure>     tasks_;
    std::mutex               mutex_;
    std::thread::id          thread_id_;
    std::vector<Channel*>    active_channels_;
    std::atomic<bool>        shut_;
    OnEventPollErrorCallback on_err_func_ = nullptr;
    std::string              name_;
  };
} // namespace cxpnet

#endif // IO_POLL_H