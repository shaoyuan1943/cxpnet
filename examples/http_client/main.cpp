#include "cxpnet/connector.h"
#include "cxpnet/io_event_poll.h"
#include "cxpnet/conn.h"
#include "cxpnet/buffer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

using namespace cxpnet;

class HttpClient {
public:
    HttpClient(const std::string& addr, uint16_t port) 
        : addr_(addr), port_(port) {
        connector_ = std::make_unique<Connector>(&event_poll_, addr_, port_);
        connector_->set_conn_user_callback([this](const ConnPtr& conn) {
            std::cout << "Connected to HTTP server" << std::endl;
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
            
            // Send HTTP GET request
            std::string request = 
                "GET / HTTP/1.1\r\n"
                "Host: " + addr_ + "\r\n"
                "Connection: close\r\n"
                "\r\n";
            
            conn->send(request);
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

private:
    void onMessage(const ConnPtr& conn, Buffer* buffer) {
        std::string response(buffer->peek(), buffer->readable_size());
        std::cout << "HTTP Response:" << std::endl;
        std::cout << response << std::endl;
        buffer->retrieve(buffer->readable_size());
        
        // Close connection after receiving response
        conn->shutdown();
    }

    void onClose(const ConnPtr& conn, int err) {
        std::cout << "HTTP connection closed with error: " << err << std::endl;
        conn_.reset();
    }

    std::string addr_;
    uint16_t port_;
    IOEventPoll event_poll_;
    std::unique_ptr<Connector> connector_;
    ConnPtr conn_;
};

int main() {
    HttpClient client("127.0.0.1", 8080);
    client.connect();
    
    // Run event loop in a separate thread
    std::thread t([&client]() {
        client.run();
    });
    
    // Wait for client to finish
    t.join();
    
    return 0;
}