#include "cxpnet/server.h"
#include "cxpnet/conn.h"
#include "cxpnet/buffer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <queue>
#include <mutex>

using namespace cxpnet;

class HttpServer {
public:
    HttpServer(const std::string& addr, uint16_t port, int thread_num = 4) 
        : server_(addr.c_str(), port, ProtocolStack::kIPv4Only, SocketOption::kReuseAddr) {
        server_.set_thread_num(thread_num);
        server_.set_conn_user_callback([this](const ConnPtr& conn) {
            std::cout << "New HTTP connection from " 
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
        std::cout << "HTTP server started, listening on port 8080" << std::endl;
        server_.run();
    }

private:
    void onMessage(const ConnPtr& conn, Buffer* buffer) {
        std::string request(buffer->peek(), buffer->readable_size());
        buffer->retrieve(buffer->readable_size());
        
        // Simple HTTP request parsing
        if (request.find("GET /") != std::string::npos) {
            std::string response = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "Connection: close\r\n"
                "\r\n"
                "<html><body><h1>Hello from cxpnet HTTP Server!</h1></body></html>";
            
            conn->send(response);
            conn->shutdown(); // Close connection after response
        } else {
            std::string response = 
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/html\r\n"
                "Connection: close\r\n"
                "\r\n"
                "<html><body><h1>404 Not Found</h1></body></html>";
            
            conn->send(response);
            conn->shutdown(); // Close connection after response
        }
    }

    void onClose(const ConnPtr& conn, int err) {
        std::cout << "HTTP connection closed from " 
                  << conn->remote_addr_and_port().first << ":" 
                  << conn->remote_addr_and_port().second 
                  << " with error: " << err << std::endl;
    }

    Server server_;
};

int main() {
    HttpServer server("127.0.0.1", 8080, 4);
    server.start();
    return 0;
}