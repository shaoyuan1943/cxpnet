#include "cxpnet/connector.h"
#include "cxpnet/io_event_poll.h"
#include "cxpnet/conn.h"
#include "cxpnet/buffer.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace cxpnet;

class TcpClient {
public:
    TcpClient(const std::string& addr, uint16_t port) 
        : addr_(addr), port_(port) {
        connector_ = std::make_unique<Connector>(&event_poll_, addr_, port_);
        connector_->set_conn_user_callback([this](const ConnPtr& conn) {
            std::cout << "Connected to server" << std::endl;
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
        });
        
        connector_->set_error_user_callback([this](int err) {
            std::cout << "Connection error: " << err << std::endl;
            // Try to reconnect after 3 seconds
            std::this_thread::sleep_for(std::chrono::seconds(3));
            connect();
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

    void send(const std::string& msg) {
        if (conn_ && conn_->connected()) {
            conn_->send(msg);
        }
    }

    void run() {
        event_poll_.run();
    }

private:
    void onMessage(const ConnPtr& conn, Buffer* buffer) {
        std::string msg(buffer->peek(), buffer->readable_size());
        std::cout << "Received: " << msg << std::endl;
        buffer->retrieve(buffer->readable_size());
    }

    void onClose(const ConnPtr& conn, int err) {
        std::cout << "Connection closed with error: " << err << std::endl;
        conn_.reset();
        // Try to reconnect after 3 seconds
        std::this_thread::sleep_for(std::chrono::seconds(3));
        connect();
    }

    std::string addr_;
    uint16_t port_;
    IOEventPoll event_poll_;
    std::unique_ptr<Connector> connector_;
    ConnPtr conn_;
};

int main() {
    TcpClient client("127.0.0.1", 9090);
    client.connect();
    
    // Run event loop in a separate thread
    std::thread t([&client]() {
        client.run();
    });
    
    // Send messages from main thread
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit") {
            client.disconnect();
            break;
        }
        client.send(line);
    }
    
    t.join();
    return 0;
}