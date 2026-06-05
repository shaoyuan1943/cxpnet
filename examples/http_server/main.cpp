#include <cxpnet/cxpnet.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {
  std::string make_response(std::string_view status, std::string_view body) {
    return "HTTP/1.1 " + std::string(status) + "\r\n"
        + "Content-Type: text/plain; charset=utf-8\r\n"
        + "Content-Length: " + std::to_string(body.size()) + "\r\n"
        + "Connection: close\r\n"
        + "\r\n"
        + std::string(body);
  }
} // namespace

int main(int argc, char* argv[]) {
  const char* host = argc > 1 ? argv[1] : "127.0.0.1";
  uint16_t    port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : 8080;
  cxpnet::Server server(host, port, cxpnet::ProtocolStack::kIPv4Only, cxpnet::SocketOption::kReuseAddr);

  server.set_thread_num(1);
  server.set_conn_user_callback([](cxpnet::ConnPtr conn) {
    conn->set_message_callback([conn](std::string_view data) {
      std::string request(data);

      if (request.starts_with("GET / ")) {
        conn->send(make_response("200 OK", "hello from cxpnet http_server\n"));
      } else {
        conn->send(make_response("404 Not Found", "not found\n"));
      }
      conn->shutdown();
    });
    conn->set_close_callback([](int err) {
      std::cout << "http connection closed: " << err << std::endl;
    });
  });

  if (!server.start(cxpnet::RunningMode::kOnePollPerThread)) {
    std::cerr << "failed to start http server" << std::endl;
    return 1;
  }

  std::cout << "http server listening on " << host << ":" << port << std::endl;
  server.run();
  return 0;
}
