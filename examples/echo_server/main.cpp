#include <cxpnet/cxpnet.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
  const char*  host = argc > 1 ? argv[1] : "127.0.0.1";
  uint16_t     port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : 9090;
  const int    poll_threads = argc > 3 ? std::atoi(argv[3]) : 1;
  cxpnet::Server server(host, port, cxpnet::ProtocolStack::kIPv4Only, cxpnet::SocketOption::kReuseAddr);

  server.set_thread_num(poll_threads);
  server.set_conn_user_callback([](cxpnet::ConnPtr conn) {
    std::cout << "client connected: " << conn->remote_addr_and_port().first
              << ":" << conn->remote_addr_and_port().second << std::endl;

    conn->set_message_callback([conn](std::string_view data) {
      conn->send(data);
    });
    conn->set_close_callback([](int err) {
      std::cout << "client closed: " << err << std::endl;
    });
  });

  if (!server.start(cxpnet::RunningMode::kOnePollPerThread)) {
    std::cerr << "failed to start echo server" << std::endl;
    return 1;
  }

  std::cout << "echo server listening on " << host << ":" << port << std::endl;
  server.run();
  return 0;
}
