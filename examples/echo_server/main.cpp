#include "cxpnet/server.h"
#include "cxpnet/conn.h"
#include "cxpnet/buffer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

using namespace cxpnet;

class EchoServer {
public:
    EchoServer(const std::string& addr, uint16_t port, int thread_num = 4) 
        : server_(addr.c_str(), port, ProtocolStack::kIPv4Only, SocketOption::kReuseAddr),
          connection_count_(0),
          message_count_(0) {
        server_.set_thread_num(thread_num);
        server_.set_conn_user_callback([this](const ConnPtr& conn) {
            connection_count_++;
            std::cout << "New connection from " 
                      << conn->remote_addr_and_port().first << ":" 
                      << conn->remote_addr_and_port().second 
                      << " (Total connections: " << connection_count_ << ")" << std::endl;
            
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
        std::cout << "Echo server started, listening on port 9092" << std::endl;
        
        // Start statistics thread
        stats_thread_ = std::thread([this]() {
            while (!shutdown_) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                printStats();
            }
        });
        
        server_.run();
    }

    void stop() {
        shutdown_ = true;
        if (stats_thread_.joinable()) {
            stats_thread_.join();
        }
        server_.shutdown();
    }

private:
    void onMessage(const ConnPtr& conn, Buffer* buffer) {
        message_count_++;
        std::string msg(buffer->peek(), buffer->readable_size());
        // Echo the message back
        conn->send(msg);
        buffer->retrieve(buffer->readable_size());
    }

    void onClose(const ConnPtr& conn, int err) {
        connection_count_--;
        std::cout << "Connection closed from " 
                  << conn->remote_addr_and_port().first << ":" 
                  << conn->remote_addr_and_port().second 
                  << " (Total connections: " << connection_count_ << ")"
                  << " with error: " << err << std::endl;
    }

    void printStats() {
        std::cout << "=== Server Statistics ===" << std::endl;
        std::cout << "Active connections: " << connection_count_ << std::endl;
        std::cout << "Total messages: " << message_count_ << std::endl;
        std::cout << "========================" << std::endl;
    }

    Server server_;
    std::atomic<int> connection_count_;
    std::atomic<long long> message_count_;
    std::thread stats_thread_;
    std::atomic<bool> shutdown_{false};
};

int main() {
    EchoServer server("127.0.0.1", 9092, 4);
    
    std::thread server_thread([&server]() {
        server.start();
    });
    
    // Wait for user input to stop server
    std::cout << "Press Enter to stop the server..." << std::endl;
    std::cin.get();
    
    server.stop();
    server_thread.join();
    
    return 0;
}