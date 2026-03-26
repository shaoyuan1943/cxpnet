#ifndef POLL_THREAD_POOL_H
#define POLL_THREAD_POOL_H

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace cxpnet {
  class IOEventPoll;
  class PollThreadPool {
  public:
    PollThreadPool(const std::vector<IOEventPoll*>& event_polls)
        : polls_(event_polls)
        , next_(0)
        , shut_(false) {}
    ~PollThreadPool() {
      if (!shut_) { shutdown(); }
    }

    PollThreadPool(const PollThreadPool&)            = delete;
    PollThreadPool& operator=(const PollThreadPool&) = delete;

    void         start();
    void         shutdown();
    IOEventPoll* next_poll();
  private:
    std::vector<IOEventPoll*>                 polls_;
    std::vector<std::unique_ptr<std::thread>> threads_;
    std::atomic<size_t>                       next_; // 改为 atomic 类型，确保线程安全
    std::atomic<bool>                         shut_;
  };
} // namespace cxpnet

#endif // POLL_THREAD_POLL_H