#include <cxpnet/cxpnet.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace {
  bool is_safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) { return false; }
    for (const auto& part : path) {
      if (part == "..") { return false; }
    }
    return true;
  }

  std::string read_file(const std::filesystem::path& root, const std::string& requested_path) {
    std::filesystem::path relative_path(requested_path);
    if (!is_safe_relative_path(relative_path)) {
      return "ERR invalid path\n";
    }

    std::ifstream file(root / relative_path, std::ios::binary);
    if (!file.is_open()) {
      return "ERR file not found\n";
    }

    std::ostringstream content;
    content << file.rdbuf();
    return "OK " + std::to_string(content.str().size()) + "\n" + content.str();
  }
} // namespace

int main(int argc, char* argv[]) {
  const char*           host = argc > 1 ? argv[1] : "127.0.0.1";
  uint16_t              port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : 9094;
  std::filesystem::path root = argc > 3 ? argv[3] : ".";
  cxpnet::Server        server(host, port, cxpnet::ProtocolStack::kIPv4Only, cxpnet::SocketOption::kReuseAddr);

  server.set_conn_user_callback([root](cxpnet::ConnPtr conn) {
    std::weak_ptr<cxpnet::Conn> weak_conn = conn;
    conn->set_message_callback([root, weak_conn, handled = false](cxpnet::Buffer* buffer) mutable {
      auto conn = weak_conn.lock();
      if (!conn) { return; }
      if (handled) { buffer->consume_all(); return; }

      constexpr size_t kMaxRequestSize = 4096;
      std::string_view data(buffer->readable_data(), buffer->readable_size());
      size_t line_end = data.find('\n');
      if (line_end == std::string_view::npos && data.size() < kMaxRequestSize) { return; }
      handled = true;

      std::string response;
      if (line_end == std::string_view::npos || line_end + 1 > kMaxRequestSize) {
        response = "ERR request too long\n";
      } else {
        std::string_view request = data.substr(0, line_end);
        if (request.ends_with('\r')) { request.remove_suffix(1); }
        if (!request.starts_with("GET ")) {
          response = "ERR unsupported command\n";
        } else {
          response = read_file(root, std::string(request.substr(4)));
        }
      }

      buffer->consume_all();
      conn->send(response);
      conn->run_later_in_poll([conn]() {
        conn->shutdown();
      });
    });
    conn->set_close_callback([](int err) {
      std::cout << "file connection closed: " << err << std::endl;
    });
  });

  if (!server.start(cxpnet::RunningMode::kOnePollPerThread, 1)) {
    std::cerr << "failed to start file server" << std::endl;
    return 1;
  }

  std::cout << "file server listening on " << host << ":" << port
            << ", root=" << root.string() << std::endl;
  server.run();
  return 0;
}
