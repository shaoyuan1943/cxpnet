#include "cxpnet/cxpnet.h"
#include <iostream>

int main() {
  using namespace cxpnet;
  Server server("127.0.0.1", 9090);
  server.set_thread_num(1);
  server.set_conn_user_callback([](ConnPtr conn){
    std::cout << "New Conn: " << conn->native_handle() << std::endl;
    conn->send("hello, I'm async_server"); 
  });

  server.set_poll_error_user_callback([](IOEventPoll* poll, int err){
    std::cout << "IOEventPoll poll: " << poll->name() << ", err: " << err << std::endl; 
  });
  
  server.start(RunningMode::kOnePollPerThread);
  server.run();
}