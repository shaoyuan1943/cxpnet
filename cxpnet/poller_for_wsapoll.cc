#include "poller_for_wsapoll.h"
#include "channel.h"
#include "check.h"
#include "io_event_poll.h"

namespace cxpnet {
  void WSAPollPoller::shutdown() { channels_.clear(); }

  void WSAPollPoller::update_channel(Channel* channel) {
    CXPNET_CHECK(owner_poll_->is_in_poll_thread(), "Unsafe cross-thread operations");

    if (channel->is_none_event()) {
      unregister_channel(channel);
    } else {
      auto [it, inserted] = channels_.emplace(channel->handle(), channel);
      CXPNET_CHECK(inserted || it->second == channel, "Duplicate channel");
    }
  }

  void WSAPollPoller::unregister_channel(Channel* channel) {
    // poll 线程退出并 join 后，析构线程还需要注销唤醒 channel。
    CXPNET_CHECK(owner_poll_->is_in_poll_thread() || !owner_poll_->is_polling(),
                 "Unsafe cross-thread operations");
    auto it = channels_.find(channel->handle());
    if (it == channels_.end()) { return; }

    CXPNET_CHECK(it->second == channel, "Duplicate channel");
    channels_.erase(it);
  }

  int WSAPollPoller::poll(int timeout, std::vector<Channel*>& active_channels) {
    CXPNET_CHECK(owner_poll_->is_in_poll_thread(), "Unsafe cross-thread operations");

    poll_fds_.clear();
    for (const auto& [fd, channel] : channels_) {
      short requested = 0;
      if (channel->is_reading()) {
        requested |= POLLRDNORM;
      }

      if (channel->is_writing()) {
        requested |= POLLWRNORM;
      }

      poll_fds_.push_back({fd, requested, 0});
    }

    int count = ::WSAPoll(poll_fds_.data(), static_cast<ULONG>(poll_fds_.size()), timeout);
    if (count <= 0) { return count; }

    for (const auto& fd : poll_fds_) {
      if (fd.revents == 0) { continue; }

      Channel* channel = channels_.at(fd.fd);
      int      result  = events::kNone;

      if (fd.revents & POLLRDNORM) {
        result |= events::kRead;
      }

      if (fd.revents & POLLWRNORM) {
        result |= events::kWrite;
      }

      if (fd.revents & (POLLERR | POLLNVAL)) {
        result |= events::kError;
      }

      if (fd.revents & POLLHUP) {
        result |= events::kHup;
        // 先把数据读完再报错
        if (channel->is_reading()) {
          result |= events::kRead;
        }
      }

      channel->set_result_events(result);
      active_channels.push_back(channel);
    }
    return count;
  }
} // namespace cxpnet
