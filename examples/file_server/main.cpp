#include "cxpnet/cxpnet.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

using namespace cxpnet;

class FileServer {
public:
  FileServer(const std::string& addr, uint16_t port, int thread_num = 4)
      : server_(addr.c_str(), port, ProtocolStack::kIPv4Only, SocketOption::kReuseAddr) {
    server_.set_thread_num(thread_num);
    server_.set_conn_user_callback([this](const ConnPtr& conn) {
      std::cout << "New file transfer connection from "
                << conn->remote_addr_and_port().first << ":"
                << conn->remote_addr_and_port().second << std::endl;

      conn->set_conn_user_callbacks(
          [this, conn](Buffer* buffer) {
            this->onMessage(conn, buffer);
          },
          [this](int err) {
            this->onClose(err);
          });
    });
  }

  void start() {
    if (!server_.start(RunningMode::kOnePollPerThread)) {
      std::cout << "Failed to start file server" << std::endl;
      return;
    }

    std::cout << "File server started, listening on port 9094" << std::endl;
    server_.run();
  }
private:
  void onMessage(const ConnPtr& conn, Buffer* buffer) {
    std::string request(buffer->peek(), buffer->readable_size());
    buffer->been_read_all();

    if (request.rfind("GET ", 0) == 0) {
      std::string filename = request.substr(4);
      filename             = filename.substr(0, filename.find_first_of("\r\n "));
      std::cout << "Received GET request for file: " << filename << std::endl;

      std::ifstream file(filename, std::ios::binary);
      if (!file.is_open()) {
        conn->send("ERROR: file not found\n");
        conn->shutdown();
        return;
      }

      std::ostringstream content;
      content << file.rdbuf();

      std::string response = "OK\n" + content.str();
      conn->send(response);
      conn->shutdown();
      return;
    }

    std::cout << "Received unknown command: " << request << std::endl;
    conn->send("ERROR: unsupported command\n");
    conn->shutdown();
  }

  void onClose(int err) {
    std::cout << "File transfer connection closed with error: " << err << std::endl;
  }

  Server server_;
};

int main() {
  FileServer server("127.0.0.1", 9094, 4);
  server.start();
  return 0;
}
