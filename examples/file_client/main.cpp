#include <cxpnet/cxpnet.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {
  bool write_response_body(std::string_view response, const std::filesystem::path& output_path) {
    constexpr std::string_view ok_prefix = "OK ";
    if (!response.starts_with(ok_prefix)) {
      std::cout << response << std::endl;
      return false;
    }

    size_t header_end = response.find('\n');
    if (header_end == std::string_view::npos) {
      std::cerr << "invalid file response" << std::endl;
      return false;
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output.is_open()) {
      std::cerr << "failed to open output file: " << output_path.string() << std::endl;
      return false;
    }

    std::string_view body = response.substr(header_end + 1);
    output.write(body.data(), static_cast<std::streamsize>(body.size()));
    std::cout << "wrote " << body.size() << " bytes to " << output_path.string() << std::endl;
    return true;
  }
} // namespace

int main(int argc, char* argv[]) {
  const char*           host = argc > 1 ? argv[1] : "127.0.0.1";
  uint16_t              port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : 9094;
  std::string           remote_path = argc > 3 ? argv[3] : "sample.txt";
  std::filesystem::path output_path = argc > 4 ? argv[4] : "downloaded_sample.txt";
  cxpnet::SampleClient  client(host, port);

  client.set_conn_user_callback([&](cxpnet::ConnPtr conn) {
    std::weak_ptr<cxpnet::Conn> weak_conn = conn;
    conn->set_message_callback([&](std::string_view response) {
      write_response_body(response, output_path);
    });
    conn->set_close_callback([&client, weak_conn](int err) {
      std::cout << "file connection closed: " << err << std::endl;
      if (auto conn = weak_conn.lock()) {
        conn->run_later_in_poll([&client]() {
          client.close();
        });
      }
    });

    client.send("GET " + remote_path + "\n");
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
