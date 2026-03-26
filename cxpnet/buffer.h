#ifndef BUFFER_H
#define BUFFER_H

#include "ensure.h"
#include "sock.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace cxpnet {
  class Buffer {
    static constexpr size_t kInitialCapacity = 8192;
    static constexpr size_t kShrinkThreshold = 16384;
  public:
    explicit Buffer(size_t initial_capacity = kInitialCapacity) {
      capacity_    = (std::max)(initial_capacity, kInitialCapacity);
      data_        = new char[capacity_];
      write_index_ = 0;
      read_index_  = 0;
    }

    Buffer(const char* data, size_t size) noexcept {
      data_ = new char[size];
      std::memcpy(data_, data, size);
      capacity_    = size;
      write_index_ = size;
      read_index_  = 0;
    }

    Buffer(const Buffer&)            = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&& other) noexcept {
      data_        = other.data_;
      read_index_  = other.read_index_;
      write_index_ = other.write_index_;
      capacity_    = other.capacity_;

      other.data_        = nullptr;
      other.read_index_  = 0;
      other.write_index_ = 0;
      other.capacity_    = 0;
    }

    Buffer& operator=(Buffer&& other) noexcept {
      if (data_ == other.data_) { return *this; }

      delete[] data_;
      data_              = other.data_;
      read_index_        = other.read_index_;
      write_index_       = other.write_index_;
      capacity_          = other.capacity_;
      other.data_        = nullptr;
      other.read_index_  = 0;
      other.write_index_ = 0;
      other.capacity_    = 0;
      return *this;
    }

    ~Buffer() { delete[] data_; }

    void   clear() { read_index_ = write_index_ = 0; }
    bool   empty() const { return readable_size() == 0; }
    size_t readable_size() const { return write_index_ - read_index_; }
    size_t writable_size() const { return capacity_ - write_index_; }

    char*       to_read() const { return data_ + read_index_; }
    const char* peek() const { return data_ + read_index_; }

    void been_read(size_t len) {
      ENSURE(len <= readable_size(), "been_read: len exceeds readable size");
      if (len == 0) { return; }
      if (len == readable_size()) {
        read_index_ = write_index_;
        shrink_if_needed_();
        return;
      }

      read_index_ += len;
      if (read_index_ >= capacity_ / 2) {
        shrink_if_needed_();
      }
    }
    void been_read_all() { been_read(readable_size()); }

    char* to_write() const { return data_ + write_index_; }
    void  been_written(size_t len) {
      ENSURE(len <= writable_size(), "been_written: len exceeds writable size");
      write_index_ += len;
    }

    void append(std::string_view data) { append(data.data(), data.size()); }
    void append(const char* data, size_t len) {
      ENSURE(len > 0, "append size must > 0");
      ensure_writable_size(len);
      std::memcpy(to_write(), data, len);
      been_written(len);
    }

    void ensure_writable_size(size_t len) {
      size_t head_size = read_index_;
      size_t tail_size = writable_size();
      if (tail_size >= len) { return; }

      if (tail_size < len) {
        if (head_size + tail_size >= len) {
          size_t written_size = readable_size();
          std::memmove(data_, data_ + read_index_, written_size);
          read_index_  = 0;
          write_index_ = written_size;
        } else {
          size_t written_size = readable_size();
          size_t new_capacity = capacity_;
          size_t required     = written_size + len;
          while (new_capacity < required) {
            new_capacity *= 2;
          }
          char* new_buffer = new char[new_capacity];
          std::memcpy(new_buffer, to_read(), written_size);
          delete[] data_;
          data_        = new_buffer;
          capacity_    = new_capacity;
          read_index_  = 0;
          write_index_ = written_size;
        }
      }
    }
  private:
    // 自动收缩：当闲置空间过大时释放内存
    void shrink_if_needed_() {
      size_t used = readable_size();
      size_t idle = capacity_ - used;

      if (used == 0) {
        if (capacity_ > kInitialCapacity * 4) {
          delete[] data_;
          capacity_ = kInitialCapacity;
          data_     = new char[capacity_];
        }
        clear();
        return;
      }

      // 闲置空间需同时满足：比例 > 75% 且绝对值 > 16KB
      if (idle * 4 <= capacity_ * 3 || idle < kShrinkThreshold) { return; }

      // 目标容量：取 (使用量, kInitialCapacity) 的较大值，预留 50% 空间
      size_t target = (std::max)(used, kInitialCapacity);
      target        = target + target / 2;

      char* new_buf = new char[target];
      std::memcpy(new_buf, data_ + read_index_, used);
      delete[] data_;

      data_        = new_buf;
      capacity_    = target;
      read_index_  = 0;
      write_index_ = used;
    }

    char*  data_        = nullptr;
    size_t write_index_ = 0;
    size_t read_index_  = 0;
    size_t capacity_    = 0;
  };
} // namespace cxpnet

#endif // BUFFER_H