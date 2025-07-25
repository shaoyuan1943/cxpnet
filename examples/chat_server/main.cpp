#include "cxpnet/server.h"
#include "cxpnet/conn.h"
#include "cxpnet/buffer.h"
#include <iostream>
#include <unordered_map>
#include <mutex>

using namespace cxpnet;

class ChatServer {
public:
    ChatServer(const std::string& addr, uint16_t port, int thread_num = 4) 
        : server_(addr.c_str(), port, ProtocolStack::kIPv4Only, SocketOption::kReuseAddr) {
        server_.set_thread_num(thread_num);
        server_.set_conn_user_callback([this](const ConnPtr& conn) {
            std::cout << "New connection from " 
                      << conn->remote_addr_and_port().first << ":" 
                      << conn->remote_addr_and_port().second << std::endl;
            
            // Add to connections map
            {
                std::lock_guard<std::mutex> lock(mutex_);
                connections_[conn->native_handle()] = conn;
            }
            
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
        std::cout << "Chat server started, listening on port 9091" << std::endl;
        server_.run();
    }

private:
    void onMessage(const ConnPtr& conn, Buffer* buffer) {
        std::string msg(buffer->peek(), buffer->readable_size());
        buffer->retrieve(buffer->readable_size());
        
        std::cout << "Message from " << conn->remote_addr_and_port().first 
                  << ": " << msg << std::endl;
        
        // Broadcast message to all clients
        broadcastMessage(msg, conn);
    }

    void onClose(const ConnPtr& conn, int err) {
        std::cout << "Connection closed from " 
                  << conn->remote_addr_and_port().first << ":" 
                  << conn->remote_addr_and_port().second 
                  << " with error: " << err << std::endl;
        
        // Remove from connections map
        {
            std::lock_guard<std::mutex> lock(mutex_);
            connections_.erase(conn->native_handle());
        }
    }

    void broadcastMessage(const std::string& msg, const ConnPtr& sender) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pair : connections_) {
            const auto& conn = pair.second;
            if (conn != sender && conn->connected()) {
                conn->send(msg);
            }
        }
    }

    Server server_;
    std::unordered_map<int, ConnPtr> connections_;
    std::mutex mutex_;
};

int main() {
    ChatServer server("127.0.0.1", 9091, 4);
    server.start();
    return 0;
}