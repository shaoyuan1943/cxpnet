#include "cxpnet/connector.h"
#include "cxpnet/io_event_poll.h"
#include "cxpnet/conn.h"
#include "cxpnet/buffer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

using namespace cxpnet;

class EchoClient {
public:
    EchoClient(const std::string& addr, uint16_t port, int message_count = 1000) 
        : addr_(addr), port_(port), message_count_(message_count),
          sent_count_(0), received_count_(0) {
        connector_ = std::make_unique<Connector>(&event_poll_, addr_, port_);
        connector_->set_conn_user_callback([this](const ConnPtr& conn) {
            std::cout << "Connected to echo server" << std::endl;
            conn_ = conn;
            
            // Set up message and close callbacks
            conn->set_conn_user_callbacks(
                [this](const ConnPtr& conn, Buffer* buffer) {
                    this->onMessage(conn, buffer);
                },
                [this](const ConnPtr& conn, int err) {
                    this->onClose(conn, err);
                }
            );
            
            // Start sending messages
            sendMessage();
        });
        
        connector_->set_error_user_callback([this](int err) {
            std::cout << "Connection error: " << err << std::endl;
        });
    }

    void connect() {
        connector_->start();
    }

    void disconnect() {
        if (conn_) {
            conn_->shutdown();
        }
    }

    void run() {
        event_poll_.run();
    }

    void printStats() {
        std::cout << "=== Client Statistics ===" << std::endl;
        std::cout << "Sent messages: " << sent_count_ << std::endl;
        std::cout << "Received messages: " << received_count_ << std::endl;
        std::cout << "========================" << std::endl;
    }

private:
    void sendMessage() {
        if (sent_count_ < message_count_ && conn_ && conn_->connected()) {
            std::string msg = "Message " + std::to_string(sent_count_ + 1);
            conn_->send(msg);
            sent_count_++;
            
            // Schedule next message
            event_poll_.run_in_poll([this]() {
                sendMessage();
            }, std::chrono::milliseconds(10)); // 10ms interval
        } else if (sent_count_ >= message_count_) {
            std::cout << "All messages sent. Waiting for responses..." << std::endl;
        }
    }

    void onMessage(const ConnPtr& conn, Buffer* buffer) {
        std::string msg(buffer->peek(), buffer->readable_size());
        received_count_++;
        buffer->retrieve(buffer->readable_size());
        
        if (received_count_ >= message_count_) {
            std::cout << "All messages received. Stopping client." << std::endl;
            printStats();
            disconnect();
        }
    }

    void onClose(const ConnPtr& conn, int err) {
        std::cout << "Connection closed with error: " << err << std::endl;
        conn_.reset();
    }

    std::string addr_;
    uint16_t port_;
    int message_count_;
    std::atomic<int> sent_count_;
    std::atomic<int> received_count_;
    IOEventPoll event_poll_;
    std::unique_ptr<Connector> connector_;
    ConnPtr conn_;
};

int main() {
    EchoClient client("127.0.0.1", 9092, 1000);
    client.connect();
    
    // Run event loop in a separate thread
    std::thread t([&client]() {
        client.run();
    });
    
    // Wait for client to finish
    t.join();
    
    client.printStats();
    return 0;
}