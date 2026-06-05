#include <cxpnet/cxpnet.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

  server.set_thread_num(1);
  server.set_conn_user_callback([root](cxpnet::ConnPtr conn) {
    conn->set_message_callback([root, conn](std::string_view data) {
      std::string request(data);

      if (!request.starts_with("GET ")) {
        conn->send("ERR unsupported command\n");
        conn->shutdown();
        return;
      }

      std::string filename = request.substr(4);
      if (!filename.empty() && filename.back() == '\n') {
        filename.pop_back();
      }
      if (!filename.empty() && filename.back() == '\r') {
        filename.pop_back();
      }

      conn->send(read_file(root, filename));
      conn->shutdown();
    });
    conn->set_close_callback([](int err) {
      std::cout << "file connection closed: " << err << std::endl;
    });
  });

  if (!server.start(cxpnet::RunningMode::kOnePollPerThread)) {
    std::cerr << "failed to start file server" << std::endl;
    return 1;
  }

  std::cout << "file server listening on " << host << ":" << port
            << ", root=" << root.string() << std::endl;
  server.run();
  return 0;
}
