#include <cxpnet/cxpnet.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
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

  server.set_conn_user_callback([](cxpnet::ConnPtr conn) {
    std::weak_ptr<cxpnet::Conn> weak_conn = conn;
    conn->set_message_callback([weak_conn, handled = false](cxpnet::Buffer* buffer) mutable {
      auto conn = weak_conn.lock();
      if (!conn) { return; }
      if (handled) { buffer->consume_all(); return; }

      constexpr size_t kMaxHeaderSize = 16384;
      std::string_view data(buffer->readable_data(), buffer->readable_size());
      size_t header_end = data.find("\r\n\r\n");
      if (header_end == std::string_view::npos && data.size() < kMaxHeaderSize) { return; }
      handled = true;

      std::string response;
      if (header_end == std::string_view::npos || header_end + 4 > kMaxHeaderSize) {
        response = make_response("431 Request Header Fields Too Large", "request header too long\n");
      } else {
        std::string_view request = data.substr(0, data.find("\r\n"));
        size_t method_end = request.find(' ');
        size_t path_end = method_end == std::string_view::npos
            ? std::string_view::npos : request.find(' ', method_end + 1);
        if (path_end == std::string_view::npos
            || (request.substr(path_end + 1) != "HTTP/1.1" && request.substr(path_end + 1) != "HTTP/1.0")) {
          response = make_response("400 Bad Request", "invalid request line\n");
        } else if (request.substr(0, method_end) == "GET"
                   && request.substr(method_end + 1, path_end - method_end - 1) == "/") {
          response = make_response("200 OK", "hello from cxpnet http_server\n");
        } else {
          response = make_response("404 Not Found", "not found\n");
        }
      }
      buffer->consume_all();
      conn->send(response);
      conn->run_later_in_poll([conn]() {
        conn->shutdown();
      });
    });
    conn->set_close_callback([](int err) {
      std::cout << "http connection closed: " << err << std::endl;
    });
  });

  if (!server.start(cxpnet::RunningMode::kOnePollPerThread, 1)) {
    std::cerr << "failed to start http server" << std::endl;
    return 1;
  }

  std::cout << "http server listening on " << host << ":" << port << std::endl;
  server.run();
  return 0;
}
