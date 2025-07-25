#include "cxpnet/connector.h"
#include "cxpnet/io_event_poll.h"
#include "cxpnet/conn.h"
#include "cxpnet/buffer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>

using namespace cxpnet;

class FileClient {
public:
    FileClient(const std::string& addr, uint16_t port) 
        : addr_(addr), port_(port) {
        connector_ = std::make_unique<Connector>(&event_poll_, addr_, port_);
        connector_->set_conn_user_callback([this](const ConnPtr& conn) {
            std::cout << "Connected to file server" << std::endl;
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
        });
    }

    void connect() {
        connector_->start();
    }

    void requestFile(const std::string& filename) {
        if (conn_ && conn_->connected()) {
            std::string request = "GET " + filename;
            conn_->send(request);
        }
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
        std::cout << "File server response:" << std::endl;
        std::cout << response << std::endl;
        buffer->retrieve(buffer->readable_size());
    }

    void onClose(const ConnPtr& conn, int err) {
        std::cout << "File transfer connection closed with error: " << err << std::endl;
        conn_.reset();
    }

    std::string addr_;
    uint16_t port_;
    IOEventPoll event_poll_;
    std::unique_ptr<Connector> connector_;
    ConnPtr conn_;
};

int main() {
    FileClient client("127.0.0.1", 9094);
    client.connect();
    
    // Run event loop in a separate thread
    std::thread t([&client]() {
        client.run();
    });
    
    // Request a file
    std::string filename;
    std::cout << "Enter filename to request: ";
    std::getline(std::cin, filename);
    client.requestFile(filename);
    
    // Wait a bit for response
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    client.disconnect();
    t.join();
    
    return 0;
}