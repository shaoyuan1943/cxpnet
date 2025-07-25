#include "cxpnet/connector.h"
#include "cxpnet/io_event_poll.h"
#include "cxpnet/conn.h"
#include "cxpnet/buffer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <random>

using namespace cxpnet;

class StressClient {
public:
    StressClient(const std::string& addr, uint16_t port, int connection_count = 100) 
        : addr_(addr), port_(port), target_connection_count_(connection_count),
          connected_count_(0), message_count_(0) {
    }

    void start() {
        // Create multiple connections
        for (int i = 0; i < target_connection_count_; ++i) {
            auto connector = std::make_unique<Connector>(&event_poll_, addr_, port_);
            
            connector->set_conn_user_callback([this](const ConnPtr& conn) {
                connected_count_++;
                conn->set_conn_user_callbacks(
                    [this](const ConnPtr& conn, Buffer* buffer) {
                        message_count_++;
                        buffer->retrieve(buffer->readable_size());
                    },
                    [this](const ConnPtr& conn, int err) {
                        connected_count_--;
                    }
                );
                
                // Send initial message
                sendRandomMessage(conn);
            });
            
            connector->set_error_user_callback([this](int err) {
                // Ignore errors during stress test
            });
            
            connectors_.push_back(std::move(connector));
        }
        
        // Start all connections
        for (auto& connector : connectors_) {
            connector->start();
        }
        
        // Start statistics thread
        stats_thread_ = std::thread([this]() {
            while (!shutdown_) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                printStats();
            }
        });
    }

    void run() {
        event_poll_.run();
    }

    void stop() {
        shutdown_ = true;
        if (stats_thread_.joinable()) {
            stats_thread_.join();
        }
    }

private:
    void sendRandomMessage(const ConnPtr& conn) {
        if (!conn->connected()) return;
        
        // Generate random message
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(10, 1000);
        
        std::string msg(dis(gen), 'A');
        for (auto& c : msg) {
            c = 'A' + (c % 26);
        }
        
        conn->send(msg);
        
        // Schedule next message
        event_poll_.run_in_poll([this, conn]() {
            sendRandomMessage(conn);
        }, std::chrono::milliseconds(100)); // 100ms interval
    }

    void printStats() {
        std::cout << "Connected: " << connected_count_ 
                  << ", Messages: " << message_count_ << std::endl;
    }

    std::string addr_;
    uint16_t port_;
    int target_connection_count_;
    std::atomic<int> connected_count_;
    std::atomic<long long> message_count_;
    IOEventPoll event_poll_;
    std::vector<std::unique_ptr<Connector>> connectors_;
    std::thread stats_thread_;
    std::atomic<bool> shutdown_{false};
};

int main() {
    StressClient client("127.0.0.1", 9093, 100); // 100 connections
    
    client.start();
    
    // Run event loop in a separate thread
    std::thread t([&client]() {
        client.run();
    });
    
    // Run stress test for 60 seconds
    std::this_thread::sleep_for(std::chrono::seconds(60));
    
    client.stop();
    t.join();
    
    return 0;
}