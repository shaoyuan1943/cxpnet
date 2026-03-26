#include "cxpnet/cxpnet.h"
#include <iostream>

int main() {
  using namespace cxpnet;
  Server server("127.0.0.1", 9090);
  server.set_thread_num(1);
  server.set_conn_user_callback([](ConnPtr conn){
    std::cout << "New Conn: " << conn->native_handle() << std::endl;

    conn->set_conn_user_callbacks([](cxpnet::Buffer* buff) {
      LOG_DEBUG("msg: {}", std::string(buff->peek(), buff->readable_size()));
      buff->been_read_all();
    }, [](int ) {
      LOG_DEBUG("Conn closed");
    });
    
    conn->send("hello, I'm server"); 
  });

  server.set_error_user_callback([](int err){
    std::cout << "Server error: " << err << std::endl; 
  });
  
  server.start(RunningMode::kOnePollPerThread);
  server.run();
}
