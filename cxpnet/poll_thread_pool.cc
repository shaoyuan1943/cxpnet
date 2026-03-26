#include "poll_thread_pool.h"
#include "ensure.h"
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

    std::thread::id current_thread_id = std::this_thread::get_id();

    for (auto poll : polls_) { poll->shutdown(); }
    for (const auto& t : threads_) {
      if (!t->joinable()) { continue; }

      if (t->get_id() == current_thread_id) {
        t->detach();
        continue;
      }

      t->join();
    }
  }

  IOEventPoll* PollThreadPool::next_poll() {
    if (polls_.empty()) { return nullptr; }

    size_t index = next_.fetch_add(1, std::memory_order_relaxed) % polls_.size();
    return polls_[index];
  }

} // namespace cxpnet
