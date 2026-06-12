#include "sample_client.h"

namespace cxpnet {
  SampleClient::SampleClient(const char* addr, uint16_t port)
      : addr_ {addr == nullptr ? "" : addr}
      , port_ {port} {
    event_poll_.set_name("sample_client_poll");
  }

  SampleClient::~SampleClient() { close(); }

  bool SampleClient::connect() {
    State expected = State::kCreated;
    if (!state_.compare_exchange_strong(expected, State::kRunning, std::memory_order_acq_rel)) { return false; }

    event_poll_.run_in_poll([this]() { connect_in_poll_(); });
    return true;
  }

  void SampleClient::run() {
    State state = get_state_();
    if (state == State::kCreated || state == State::kClosed) { return; }

    event_poll_.run();
  }

  void SampleClient::poll() {
    State state = get_state_();
    if (state == State::kCreated || state == State::kClosed) { return; }

    event_poll_.poll();
  }

  void SampleClient::shutdown() {
    State state = get_state_();
    while (true) {
      if (state == State::kClosed) { return; }
      if (state == State::kClosing) { break; }

      State next_state = state == State::kCreated ? State::kClosed : State::kClosing;
      if (state_.compare_exchange_weak(state, next_state, std::memory_order_acq_rel)) {
        if (next_state == State::kClosed) { return; }
        break;
      }
    }

    ConnPtr conn_snapshot;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      conn_snapshot = conn_;
    }

    if (conn_snapshot) { conn_snapshot->shutdown(); }
  }

  void SampleClient::close() {
    State old_state = state_.exchange(State::kClosed, std::memory_order_acq_rel);
    if (old_state == State::kClosed) { return; }

    ConnPtr conn_snapshot;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      conn_snapshot = conn_;
      conn_.reset();
    }

    if (conn_snapshot) { conn_snapshot->close(); }

    event_poll_.shutdown();
  }

  void SampleClient::send(const char* msg, size_t size) {
    ConnPtr conn_snapshot;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      conn_snapshot = conn_;
    }

    if (conn_snapshot) { conn_snapshot->send(msg, size); }
  }

  void SampleClient::send(std::string_view msg) {
    send(msg.data(), msg.size());
  }

  bool SampleClient::is_connected() const {
    ConnPtr conn_snapshot;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      conn_snapshot = conn_;
    }

    return conn_snapshot && conn_snapshot->is_connected();
  }

  ConnPtr SampleClient::conn() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return conn_;
  }

  bool SampleClient::is_active_() const {
    return get_state_() == State::kRunning;
  }

  void SampleClient::connect_in_poll_() {
    if (!is_active_()) { return; }

    auto conn = std::make_shared<Conn>(&event_poll_);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      conn_ = conn;
    }

    auto on_connected     = [this](ConnPtr connected_conn) { on_connected_(connected_conn); };
    auto on_connect_error = [this](int err) { on_connect_error_(err); };

    conn->connect(addr_.c_str(), port_, on_connected, on_connect_error);
  }

  void SampleClient::on_connected_(ConnPtr conn) {
    std::function<void(ConnPtr)> on_conn_func;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!is_active_()) { return; }

      conn_        = conn;
      on_conn_func = on_conn_func_;
    }

    if (on_conn_func) { on_conn_func(conn); }
  }

  void SampleClient::on_connect_error_(int err) {
    std::function<void(int)> on_error_func;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!is_active_()) { return; }

      on_error_func = on_error_func_;
    }

    if (on_error_func) { on_error_func(err); }
  }
} // namespace cxpnet
