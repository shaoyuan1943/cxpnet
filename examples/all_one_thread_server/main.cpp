#include <cxpnet/cxpnet.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>

int main(int argc, char* argv[]) {
  const char* host = argc > 1 ? argv[1] : "127.0.0.1";
  uint16_t    port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : 9095;
  cxpnet::Server server(host, port, cxpnet::ProtocolStack::kIPv4Only, cxpnet::SocketOption::kReuseAddr);

  server.set_conn_user_callback([](cxpnet::ConnPtr conn) {
    std::weak_ptr<cxpnet::Conn> weak_conn = conn;
    conn->set_message_callback([weak_conn](std::string_view data) {
      if (auto conn = weak_conn.lock()) {
        conn->send(data);
      }
    });
    conn->set_close_callback([](int err) {
      std::cout << "client closed: " << err << std::endl;
    });
  });

  if (!server.start(cxpnet::RunningMode::kAllOneThread)) {
    std::cerr << "failed to start all-one-thread server" << std::endl;
    return 1;
  }

  std::cout << "all-one-thread server listening on " << host << ":" << port << std::endl;
  while (true) {
    server.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
