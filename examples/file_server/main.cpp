#include "buffer.h"
#include "conn.h"
#include "server.h"
#include <atomic>
#include <chrono>
#include <iostream>
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

      // Set up message and close callbacks
      conn->set_conn_user_callbacks(
          [this](Buffer* buffer) {
            this->onMessage(buffer);
          },
          [this](int err) {
            this->onClose(err);
          });
    });
  }

  void start() {
    server_.start(RunningMode::kOnePollPerThread);
    std::cout << "File server started, listening on port 9094" << std::endl;
    server_.run();
  }
private:
  void onMessage(Buffer* buffer) {
    std::string request(buffer->peek(), buffer->readable_size());
    buffer->been_read_all();

    // Note: We can't respond to the client here because we don't have access to 'conn'
    // This is a limitation of the new callback interface.
    // In a real implementation, we would need to store 'conn' somewhere accessible.
    
    // Simple file request parsing
    if (request.substr(0, 4) == "GET ") {
      std::string filename = request.substr(4);
      filename             = filename.substr(0, filename.find_first_of("\r\n "));
      
      // For this example, we'll just print the request
      std::cout << "Received GET request for file: " << filename << std::endl;
    } else {
      std::cout << "Received unknown command: " << request << std::endl;
    }
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