#include <cxpnet/cxpnet.h>

#include <iostream>

int main() {
  cxpnet::IOEventPoll poll;

  poll.timer_manager()->add_timer(100, []() {
    std::cout << "first timer fired" << std::endl;
  });

  poll.timer_manager()->add_timer(200, [&poll]() {
    std::cout << "second timer fired, stopping poll" << std::endl;
    poll.shutdown();
  });

  poll.run();
  return 0;
}
