#include "poll_thread_pool.h"
#include "io_event_poll.h"

namespace cxpnet {
  void PollThreadPool::start() {
    if (polls_.empty()) { return; }

    threads_.reserve(polls_.size());
    for (IOEventPoll* poll : polls_) {
      threads_.emplace_back(std::make_unique<std::thread>([poll]() {
        poll->run();
      }));
    }
  }
  void PollThreadPool::shutdown() {
    if (shut_.exchange(true)) { return; }

    for (auto poll : polls_) { poll->shutdown(); }
    for (auto& t : threads_) {
      if (t->joinable()) { t->join(); }
    }
  }
  IOEventPoll* PollThreadPool::next_poll() {
    if (polls_.empty()) { return nullptr; }

    IOEventPoll* selected = polls_[next_];
    next_                 = (next_ + 1) % polls_.size();
    return selected;
  }

} // namespace cxpnet
