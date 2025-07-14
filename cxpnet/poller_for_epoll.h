#ifndef POLLER_FOR_EPOLL_H
#define POLLER_FOR_EPOLL_H

#include "channel.h"
#include "io_base.h"
#include "io_event_poll.h"
#include <cassert>
#include <unordered_map>

namespace cxpnet {
  class Poller {
  public:
    Poller(IOEventPoll* owner_poll) {
      owner_poll_ = owner_poll;
      epoll_fd_   = ::epoll_create1(EPOLL_CLOEXEC);
      events_.resize(max_epoll_event_count);
    }

    ~Poller() {
      platform::close_handle(epoll_fd_);
    }

    void shutdown() {
      for (auto&& kv : channels_) {
        remove_channel(kv.second);
      }

      channels_.clear();
    }

    int poll(int timeout, std::vector<Channel*>& active_channels) {
      int n      = epoll_wait(epoll_fd_, &*events_.begin(), static_cast<int>(events_.size()), timeout);      
      if (n > 0) {
        fill_active_channels(n, active_channels);
        if (static_cast<size_t>(n) == events_.size()) { events_.resize(events_.size() * 2); }
      }

      return n;
    }

    void fill_active_channels(int event_n, std::vector<Channel*>& active_channels) {
      for (int i = 0; i < event_n; ++i) {
        // 从 epoll_event 中取出 Channel 指针
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);

        // --- 健壮性检查 ---
        // 确认这个 channel 还在我们的 map 中
        auto iter = channels_.find(channel->handle());
        assert(iter != channels_.end() && iter->second == channel);

        // 将发生的事件记录到 Channel 对象中
        channel->set_result_events(events_[i].events);
        active_channels.push_back(channel);
      }
    }

    void update_channel(Channel* channel) {
      int          handle = channel->handle();
      ChannelState state  = static_cast<ChannelState>(channel->state());
      // TODO: 由于设计上不可复用，理论上ChannelState::kDeleted时不能再执行了
      if (state == ChannelState::kNew || state == ChannelState::kDeleted) {
        if (state == ChannelState::kNew) {
          assert(channels_.find(handle) == channels_.end());
          channels_[handle] = channel;
        } else {
          assert(channels_.find(handle) == channels_.end());
          assert(channels_[handle] == channel);
        }

        channel->set_state(static_cast<int>(ChannelState::kAdded));
        update(EPOLL_CTL_ADD, channel);
      } else {
        assert(channels_.find(handle) != channels_.end());
        assert(channels_[handle] == channel);

        if (channel->is_none_event()) {
          update(EPOLL_CTL_DEL, channel);
          channel->set_state(static_cast<int>(ChannelState::kDeleted));
        } else {
          // 修改监听的事件
          update(EPOLL_CTL_MOD, channel);
        }
      }
    }
    void remove_channel(Channel* channel) {
      int handle = channel->handle();
      assert(channels_.find(handle) != channels_.end());
      assert(channels_[handle] == channel);
      assert(channel->is_none_event()); // 确认 Channel 已经不关心任何事件

      ChannelState state = static_cast<ChannelState>(channel->state());
      assert(state == ChannelState::kAdded || state == ChannelState::kDeleted);

      size_t n = channels_.erase(handle);
      assert(n == 1);

      if (state == ChannelState::kAdded) {
        // 如果它还在 epoll 的监听列表中，就移除它
        update(EPOLL_CTL_DEL, channel);
      }
      channel->set_state(static_cast<int>(ChannelState::kNew));
    }
    bool has_channel(int handle) { return channels_.count(handle) > 0; }
    void update(int op, Channel* channel) {
      struct epoll_event event;
      memset(&event, 0, sizeof(event));
      event.events   = channel->events() | EPOLLET;
      event.data.ptr = channel;
      if (epoll_ctl(epoll_fd_, op, channel->handle(), &event) < 0) {
        assert(false);
      }
    }
  private:
    IOEventPoll* owner_poll_ = nullptr;
    int          epoll_fd_   = -1;

    std::unordered_map<int, Channel*> channels_;
    std::vector<struct epoll_event>   events_;

    // clang-format off
    enum class ChannelState { kNew, kAdded, kDeleted };
    // clang-format on
  };
} // namespace cxpnet

#endif // POLLER_FOR_EPOLL_H