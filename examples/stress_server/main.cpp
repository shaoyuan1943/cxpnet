#include "cxpnet/server.h"
#include "cxpnet/conn.h"
#include "cxpnet/buffer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

using namespace cxpnet;

class StressServer {
public:
    StressServer(const std::string& addr, uint16_t port, int thread_num = 4) 
        : server_(addr.c_str(), port, ProtocolStack::kIPv4Only, SocketOption::kReuseAddr),
          connection_count_(0),
          message_count_(0) {
        server_.set_thread_num(thread_num);
        server_.set_conn_user_callback([this](const ConnPtr& conn) {
            connection_count_++;
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
        std::cout << "Stress test server started, listening on port 9093" << std::endl;
        
        // Start statistics thread
        stats_thread_ = std::thread([this]() {
            while (!shutdown_) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
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
        // Just echo back the data without copying
        conn->send(*buffer);
        buffer->retrieve(buffer->readable_size());
    }

    void onClose(const ConnPtr& conn, int err) {
        connection_count_--;
    }

    void printStats() {
        std::cout << "Connections: " << connection_count_ 
                  << ", Messages: " << message_count_ << std::endl;
    }

    Server server_;
    std::atomic<int> connection_count_;
    std::atomic<long long> message_count_;
    std::thread stats_thread_;
    std::atomic<bool> shutdown_{false};
};

int main() {
    StressServer server("127.0.0.1", 9093, 8);
    
    std::thread server_thread([&server]() {
        server.start();
    });
    
    // Run for 60 seconds
    std::this_thread::sleep_for(std::chrono::seconds(60));
    
    server.stop();
    server_thread.join();
    
    return 0;
}