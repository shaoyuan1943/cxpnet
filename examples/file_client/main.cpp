#include <cxpnet/cxpnet.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

int main(int argc, char* argv[]) {
  const char*           host = argc > 1 ? argv[1] : "127.0.0.1";
  uint16_t              port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : 9094;
  std::string           remote_path = argc > 3 ? argv[3] : "sample.txt";
  std::filesystem::path output_path = argc > 4 ? argv[4] : "downloaded_sample.txt";
  std::ofstream         output;
  std::optional<size_t> remaining;
  bool                  finished = false;
  bool                  success = false;
  cxpnet::SampleClient  client(host, port);

  client.set_conn_user_callback([&](cxpnet::ConnPtr conn) {
    std::weak_ptr<cxpnet::Conn> weak_conn = conn;
    conn->set_message_callback([&, weak_conn](cxpnet::Buffer* buffer) {
      if (finished) { buffer->consume_all(); return; }

      auto finish = [&](bool ok, std::string_view error) {
        finished = true;
        success = ok;
        if (!ok) { std::cerr << error << std::endl; }
        if (auto conn = weak_conn.lock()) {
          conn->run_later_in_poll([&client]() { client.close(); });
        }
      };

      if (!remaining) {
        constexpr size_t kMaxHeaderSize = 4096;
        std::string_view response(buffer->readable_data(), buffer->readable_size());
        size_t header_end = response.find('\n');
        if (header_end == std::string_view::npos && response.size() < kMaxHeaderSize) { return; }
        if (header_end == std::string_view::npos || header_end + 1 > kMaxHeaderSize) {
          finish(false, "file response header too long");
          return;
        }
        std::string_view header = response.substr(0, header_end);
        if (header.ends_with('\r')) { header.remove_suffix(1); }
        if (header.starts_with("ERR ")) { finish(false, header); return; }
        if (!header.starts_with("OK ")) { finish(false, "invalid file response"); return; }

        auto length = header.substr(3);
        size_t size = 0;
        auto result = std::from_chars(length.data(), length.data() + length.size(), size);
        if (result.ec != std::errc{} || result.ptr != length.data() + length.size()) {
          finish(false, "invalid file length");
          return;
        }
        output.open(output_path, std::ios::binary);
        if (!output) { finish(false, "failed to open output file"); return; }
        remaining = size;
        buffer->consume(header_end + 1);
      }

      size_t count = (std::min)(*remaining, buffer->readable_size());
      if (count != 0) {
        output.write(buffer->readable_data(), static_cast<std::streamsize>(count));
        if (!output) { finish(false, "failed to write output file"); return; }
        buffer->consume(count);
        *remaining -= count;
      }
      if (*remaining == 0) {
        output.close();
        if (!output) { finish(false, "failed to close output file"); return; }
        std::cout << "download complete: " << output_path.string() << std::endl;
        finish(true, {});
      }
    });
    conn->set_close_callback([&, weak_conn](int err) {
      std::cout << "file connection closed: " << err << std::endl;
      if (!finished) {
        finished = true;
        std::cerr << "connection closed before the complete file response" << std::endl;
      }
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
  return success ? 0 : 1;
}
