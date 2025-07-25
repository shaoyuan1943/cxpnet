#include "cxpnet/buffer.h"
#include "cxpnet/ensure.h"

#include <format>
#include <iostream>
#include <string>

template <typename... Args>
void out(std::string fmt, Args&&... args) {
  std::string msg = std::vformat(fmt, std::make_format_args(args...));
  std::cout << msg << std::endl;
}

int main() {
  out("hello world");

  cxpnet::Buffer buffer(10);
  out("init, readable_size: {}, writable_size: {}", 
    buffer.readable_size(), buffer.writable_size());

  buffer.append("abcde", 5);
  out("append after, readable_size: {}, writable_size: {}", 
    buffer.readable_size(), buffer.writable_size());

  buffer.been_read(3);  //de
  out("been_readed after, readable_size: {}, writable_size: {}, content: {}", 
    buffer.readable_size(), buffer.writable_size(), std::string(buffer.peek(), buffer.readable_size()));

  buffer.append("fghuik");
  out("append after, readable_size: {}, writable_size: {}", 
    buffer.readable_size(), buffer.writable_size());

  auto s = buffer.peek();
  auto l = buffer.readable_size();

  std::string str(s, l);
  std::cout << "str: " << str << std::endl;
  out("end, readable_size: {}, writable_size: {}, content: {}",
      buffer.readable_size(), buffer.writable_size(), std::string(buffer.peek(), buffer.readable_size()));
  
  buffer.append("12345678901234567");
  out("last, readable_size: {}, writable_size: {}, content: {}",
      buffer.readable_size(), buffer.writable_size(), std::string(buffer.peek(), buffer.readable_size()));
}