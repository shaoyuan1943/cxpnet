#include "cxpnet/server.h"
#include "cxpnet/conn.h"
#include "cxpnet/buffer.h"
#include <iostream>
#include <thread>

using namespace cxpnet;

class TcpServer {
public:
    TcpServer(const std::string& addr, uint16_t port, int thread_num = 4) 
        : server_(addr.c_str(), port, ProtocolStack::kIPv4Only, SocketOption::kReuseAddr) {
        server_.set_thread_num(thread_num);
        server_.set_conn_user_callback([this](const ConnPtr& conn) {
            std::cout << "New connection from " 
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
        std::cout << "Server started, listening on port 9090" << std::endl;
        server_.run();
    }

private:
    void onMessage(const ConnPtr& conn, Buffer* buffer) {
        std::string msg(buffer->peek(), buffer->readable_size());
        std::cout << "Received: " << msg << std::endl;
        
        // Echo the message back
        conn->send(msg);
        buffer->retrieve(buffer->readable_size());
    }

    void onClose(const ConnPtr& conn, int err) {
        std::cout << "Connection closed from " 
                  << conn->remote_addr_and_port().first << ":" 
                  << conn->remote_addr_and_port().second 
                  << " with error: " << err << std::endl;
    }

    Server server_;
};

int main() {
    TcpServer server("127.0.0.1", 9090, 4);
    server.start();
    return 0;
}