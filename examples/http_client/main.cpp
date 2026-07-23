#include <cxpnet/cxpnet.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

int main(int argc, char* argv[]) {
  const char* host = argc > 1 ? argv[1] : "127.0.0.1";
  uint16_t    port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : 8080;
  std::string path = argc > 3 ? argv[3] : "/";

  cxpnet::SampleClient client(host, port);

  client.set_conn_user_callback([&](cxpnet::ConnPtr conn) {
    std::weak_ptr<cxpnet::Conn> weak_conn = conn;
    conn->set_message_callback([&](std::string_view response) {
      std::cout << response << std::endl;
    });
    conn->set_close_callback([&client, weak_conn](int err) {
      std::cout << "http connection closed: " << err << std::endl;
      if (auto conn = weak_conn.lock()) {
        conn->run_later_in_poll([&client]() {
          client.close();
        });
      }
    });

    std::string request = "GET " + path + " HTTP/1.1\r\n"
        + "Host: " + host + "\r\n"
        + "Connection: close\r\n"
        + "\r\n";
    client.send(request);
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
