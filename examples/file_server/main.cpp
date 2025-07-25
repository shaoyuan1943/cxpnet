#include "cxpnet/server.h"
#include "cxpnet/conn.h"
#include "cxpnet/buffer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

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
                [this](const ConnPtr& conn, Buffer* buffer) {
                    this->onMessage(conn, buffer);
                },
                [this](const ConnPtr& conn, int err) {
                    this->onClose(conn, err);
                }
            );
        });
    }

    void start() {
        server_.start(RunningMode::kOnePollPerThread);
        std::cout << "File server started, listening on port 9094" << std::endl;
        server_.run();
    }

private:
    void onMessage(const ConnPtr& conn, Buffer* buffer) {
        std::string request(buffer->peek(), buffer->readable_size());
        buffer->retrieve(buffer->readable_size());
        
        // Simple file request parsing
        if (request.substr(0, 4) == "GET ") {
            std::string filename = request.substr(4);
            filename = filename.substr(0, filename.find_first_of("\r\n "));
            
            // In a real implementation, you would read the file here
            // For this example, we'll just send a dummy response
            std::string response = "FILE_CONTENT: This is the content of " + filename;
            conn->send(response);
        } else {
            std::string response = "ERROR: Unknown command";
            conn->send(response);
        }
    }

    void onClose(const ConnPtr& conn, int err) {
        std::cout << "File transfer connection closed from " 
                  << conn->remote_addr_and_port().first << ":" 
                  << conn->remote_addr_and_port().second 
                  << " with error: " << err << std::endl;
    }

    Server server_;
};

int main() {
    FileServer server("127.0.0.1", 9094, 4);
    server.start();
    return 0;
}