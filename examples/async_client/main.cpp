
#include "client.h"
#include <iostream>

int main() {
  using namespace cxpnet;
  IOEventPoll event_poll;
  Client client(&event_poll, "127.0.0.1", 9090);
  client.connect();
  event_poll.run();   
}