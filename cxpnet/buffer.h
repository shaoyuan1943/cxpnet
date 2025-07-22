#ifndef BUFFER_H
#define BUFFER_H

#include "base_type_value.h"
#include "ensure.h"

#include <cassert>
#include <cstring>

namespace cxpnet {
  class Buffer {
  public:
    Buffer(size_t initial_capacity = 8192) {
      capacity_ = initial_capacity;
      data_     = new char[capacity_];
    }
    Buffer(const Buffer&)            = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept {
      data_        = other.data_;
      read_index_  = other.read_index_;
      write_index_ = other.write_index_;
      capacity_    = other.capacity_;
      other.data_  = nullptr;
    }
    Buffer& operator=(Buffer&& other) noexcept {
      if (data_ == other.data_) { return *this; }

      delete[] data_;
      data_        = other.data_;
      read_index_  = other.read_index_;
      write_index_ = other.write_index_;
      capacity_    = other.capacity_;
      other.data_  = nullptr;
      return *this;
    }
    ~Buffer() { delete[] data_; }

    void   clear() { read_index_ = write_index_ = 0; }
    size_t readable_size() const { return write_index_ - read_index_; }
    size_t writable_size() const { return capacity_ - write_index_; }
    // read data, at the most time, use readable_size get data length
    const char* peek() const { return data_ + read_index_; }
    // if readed in OnMessageCallback 
    // must invoke been_readed or retrieve tell Buffer your readed length
    void  been_readed(size_t len) {
      ENSURE(len <= readable_size(), "len: {} > readable_size: {}", len, readable_size());
      read_index_ += len;
    }
    void retrieve(size_t len) { been_readed(len); }
    // write data
    char* begin_write() { return data_ + write_index_; }

    void been_written(size_t len) {
      ENSURE(len <= writable_size(), "len: {} > writable_size: {}", len, writable_size());
      write_index_ += len;
    }
    void append(const std::string& data) { append(data.data(), data.size()); }
    void append(std::string_view data) { append(data.data(), data.size()); };
    void append(const char* data, size_t len) {
      ENSURE(len > 0, "append size must > 0");
      ensure_writable_size(len);
      std::memcpy(begin_write(), data, len);
      write_index_ += len;
    }

    void ensure_writable_size(size_t len) {
      size_t head_size = read_index_;
      size_t tail_size = writable_size();
      if (tail_size < len) {
        if (head_size + tail_size >= len) {
          size_t written_size = write_index_ - read_index_;
          std::memmove(data_, data_ + read_index_, written_size);
          read_index_  = 0;
          write_index_ = written_size;
        } else {
          size_t written_size = readable_size();
          size_t new_capacity = capacity_ * 2 + len;
          const char* pre = peek();
          char*  new_buffer   = new char[new_capacity];
          std::memcpy(new_buffer, pre, written_size);
          delete[] data_;

          data_        = new_buffer;
          capacity_    = new_capacity;
          read_index_  = 0;
          write_index_ = written_size;
        }
      }
    }
  private:
    char*  data_        = nullptr;
    size_t write_index_ = 0;
    size_t read_index_  = 0;
    size_t capacity_    = 0;
  };
} // namespace cxpnet

#endif // BUFFER_H
