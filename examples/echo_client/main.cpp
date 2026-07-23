#include <cxpnet/cxpnet.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

int main(int argc, char* argv[]) {
  const char* host    = argc > 1 ? argv[1] : "127.0.0.1";
  uint16_t    port    = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : 9090;
  std::string message = argc > 3 ? argv[3] : "hello from SampleClient";

  cxpnet::SampleClient client(host, port);

  client.set_conn_user_callback([&](cxpnet::ConnPtr conn) {
    std::weak_ptr<cxpnet::Conn> weak_conn = conn;
    conn->set_message_callback([&client, weak_conn](std::string_view response) {
      std::cout << "echo response: " << response << std::endl;
      if (auto conn = weak_conn.lock()) {
        conn->run_later_in_poll([&client]() {
          client.close();
        });
      }
    });
    conn->set_close_callback([&client, weak_conn](int err) {
      std::cout << "connection closed: " << err << std::endl;
      if (auto conn = weak_conn.lock()) {
        conn->run_later_in_poll([&client]() {
          client.close();
        });
      }
    });

    client.send(message);
  });

  client.set_error_user_callback([&](int err) {
    std::cerr << "connect failed: " << err << std::endl;
    if (auto conn = client.conn()) {
      conn->run_later_in_poll([&client]() {
        client.close();
      });
    }
  });

  if (!client.connect()) {
    std::cerr << "client connect request was rejected" << std::endl;
    return 1;
  }

  client.run();
  return 0;
}
