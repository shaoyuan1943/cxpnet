#include <cxpnet/cxpnet.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

int main(int argc, char* argv[]) {
  const char* host    = argc > 1 ? argv[1] : "127.0.0.1";
  uint16_t    port    = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : 9090;
  std::string message = argc > 3 ? argv[3] : "hello from Conn";

  cxpnet::IOEventPoll poll;
  auto                conn = std::make_shared<cxpnet::Conn>(&poll);

  conn->connect(
      host,
      port,
      [&](cxpnet::ConnPtr connected_conn) {
        std::weak_ptr<cxpnet::Conn> weak_conn = connected_conn;
        connected_conn->set_message_callback([weak_conn](cxpnet::Buffer* buffer) {
          std::string response(buffer->readable_data(), buffer->readable_size());
          buffer->consume_all();
          std::cout << "echo response: " << response << std::endl;
          if (auto conn = weak_conn.lock()) {
            conn->run_later_in_poll([conn]() {
              conn->shutdown();
            });
          }
        });
        connected_conn->set_close_callback([&](int err) {
          std::cout << "connection closed: " << err << std::endl;
          poll.shutdown();
        });

        connected_conn->send(message);
      },
      [&](int err) {
        std::cerr << "connect failed: " << err << std::endl;
        poll.shutdown();
      });

  poll.run();
  return 0;
}
