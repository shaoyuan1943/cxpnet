#ifndef POLL_THREAD_POOL_H
#define POLL_THREAD_POOL_H

#include "io_event_poll.h"

namespace cxpnet {
  class PollThreadPool {
  public:
    PollThreadPool(const std::vector<IOEventPoll*>& event_polls)
        : polls_(event_polls)
        , next_(0)
        , shut_(false) {
    }
    ~PollThreadPool() {
      if (!shut_) { shutdown(); }
    }

    PollThreadPool(const PollThreadPool&)            = delete;
    PollThreadPool& operator=(const PollThreadPool&) = delete;

    void start() {
      if (polls_.empty()) { return; }

      threads_.reserve(polls_.size());
      for (IOEventPoll* poll : polls_) {
        threads_.emplace_back(std::make_unique<std::thread>([poll]() {
          poll->run();
        }));
      }
    }
    void shutdown() {
      if (shut_.exchange(true)) { return; }

      for (auto poll : polls_) { poll->shutdown(); }
      for (auto& t : threads_) {
        if (t->joinable()) { t->join(); }
      }
    }
    IOEventPoll* next_poll() {
      if (polls_.empty()) { return nullptr; }

      IOEventPoll* selected = polls_[next_];
      next_                 = (next_ + 1) % polls_.size();
      return selected;
    }
  private:
    std::vector<IOEventPoll*>                 polls_;
    std::vector<std::unique_ptr<std::thread>> threads_;
    size_t                                    next_;
    std::atomic<bool>                         shut_;
  };
} // namespace cxpnet

#endif // POLL_THREAD_POLL_H