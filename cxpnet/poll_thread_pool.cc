#include "poll_thread_pool.h"
#include "check.h"
#include "io_event_poll.h"

#include <latch>

namespace cxpnet {
  void PollThreadPool::start() {
    if (polls_.empty()) { return; }

    threads_.reserve(polls_.size());
    auto ready = std::make_shared<std::latch>(static_cast<std::ptrdiff_t>(polls_.size()));
    for (IOEventPoll* poll : polls_) {
      // 首个连接投递前，必须确认 worker 已接管 poll 的线程身份。
      poll->run_later([ready]() { ready->count_down(); });

      threads_.emplace_back(std::make_unique<std::thread>([poll]() {
        poll->run();
      }));
    }

    ready->wait();
  }

  void PollThreadPool::shutdown() {
    if (closed_.exchange(true)) { return; }

    // Server 的关闭流程保证不会从 poll 线程调到这里；直接断言而不是 detach，
    // 避免 detached 线程在 IOEventPoll 析构后仍访问它
    std::thread::id current_thread_id = std::this_thread::get_id();
    for (const auto& t : threads_) {
      CXPNET_CHECK(!t->joinable() || t->get_id() != current_thread_id,
                   "PollThreadPool::shutdown must not be called from a poll thread");
    }

    for (auto poll : polls_) {
      poll->shutdown();
    }

    for (const auto& t : threads_) {
      if (t->joinable()) {
        t->join();
      }
    }
  }

  IOEventPoll* PollThreadPool::next_poll() {
    if (polls_.empty()) { return nullptr; }

    size_t index = next_.fetch_add(1, std::memory_order_relaxed) % polls_.size();
    return polls_[index];
  }

} // namespace cxpnet
