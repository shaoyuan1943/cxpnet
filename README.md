# cxpnet

A simple, lightweight, cross-platform C++20 Reactor network library for Linux epoll and macOS kqueue.

## Project Structure

- `cxpnet/` Library headers and source files.
- `examples/` User-facing source-built examples.
- `CMakeLists.txt` Top-level CMake project and install rules.
- `build_linux.sh` Linux build helper.
- `build_macos.sh` macOS build helper.

## Use cxpnet From Source

The recommended source-build usage is to add this repository to your CMake build and link the exported source target:

```cmake
add_subdirectory(path/to/cxpnet)
target_link_libraries(your_target PRIVATE cxpnet::cxpnet)
```

Then include the public umbrella header:

```cpp
#include <cxpnet/cxpnet.h>
```

Linux builds require a C++20 toolchain with `std::format` support. On Ubuntu 20.04, configure CMake with:

```bash
cmake -S <repo-root-path> -B <repo-root-path>/build/debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-13
```

`Server` and `Conn` are one-shot objects by convention. After `shutdown()/close()` or a failed connect/start path, create a new object instead of reusing the old one.

`SampleClient` is a tiny single-connection client wrapper. For reconnect logic, connection pools, protocol clients, or other advanced behavior, compose directly with `Conn` and `IOEventPoll`.

`Conn` read handling has two callback forms:

- `set_message_callback(std::function<void(std::string_view)>)` is the high-level form. The view is valid only during the callback, and cxpnet consumes all currently readable bytes after the callback returns.
- `set_message_callback(std::function<void(Buffer*)>)` is the low-level form. Use it for protocol parsers that need partial consumption with `been_read(n)` or `been_read_all()`.

Use `set_close_callback(...)` separately for connection close events.

If you need the project check macro directly, include `#include <cxpnet/check.h>` and use `CXPNET_CHECK(condition, fmt, ...)`. In Debug it asserts; in Release it throws `std::runtime_error`.

## Build

Linux:

```bash
bash build_linux.sh debug clean
bash build_linux.sh debug lib
bash build_linux.sh debug examples
bash build_linux.sh debug all
bash build_linux.sh debug echo_server
```

macOS:

```bash
bash build_macos.sh debug clean
bash build_macos.sh debug lib
bash build_macos.sh debug examples
bash build_macos.sh debug all
bash build_macos.sh debug echo_server
```

The build scripts compile cxpnet and examples from source. They do not copy binaries back into `examples/`.

Example binary paths:

```text
build/debug/examples/echo_server/echo_serverd
build/release/examples/echo_server/echo_server
```

## Examples

All examples link against `cxpnet::cxpnet` from this source tree.

- `echo_server`: minimal TCP echo server using `Server`.
- `echo_client`: minimal echo client using `SampleClient`.
- `manual_conn_client`: low-level client using `Conn` and `IOEventPoll` directly.
- `all_one_thread_server`: echo server driven by repeated `Server::poll()`.
- `http_server`: minimal HTTP server on top of TCP callbacks.
- `http_client`: HTTP GET client using `SampleClient`.
- `file_server`: simple `GET <path>` file transfer server.
- `file_client`: file transfer client that writes the response body to disk.
- `timer_poll`: timer callbacks on `IOEventPoll`.

Typical local run:

```bash
bash build_linux.sh debug examples

./build/debug/examples/echo_server/echo_serverd
./build/debug/examples/echo_client/echo_clientd 127.0.0.1 9090
```

HTTP:

```bash
./build/debug/examples/http_server/http_serverd 127.0.0.1 8080
./build/debug/examples/http_client/http_clientd 127.0.0.1 8080 /
```

File transfer:

```bash
./build/debug/examples/file_server/file_serverd 127.0.0.1 9094 /tmp
./build/debug/examples/file_client/file_clientd 127.0.0.1 9094 sample.txt downloaded_sample.txt
```

## Minimal Server Example

```cpp
#include <cxpnet/cxpnet.h>

#include <string_view>

int main() {
  cxpnet::Server server("127.0.0.1", 9090);
  server.set_thread_num(1);
  server.set_conn_user_callback([](cxpnet::ConnPtr conn) {
    conn->set_message_callback([conn](std::string_view data) {
      conn->send(data);
    });
    conn->set_close_callback([](int) {});
  });

  if (!server.start(cxpnet::RunningMode::kOnePollPerThread)) {
    return 1;
  }

  server.run();
  return 0;
}
```

## Minimal SampleClient Example

```cpp
#include <cxpnet/cxpnet.h>

#include <string_view>

int main() {
  cxpnet::SampleClient client("127.0.0.1", 9090);
  client.set_conn_user_callback([&](cxpnet::ConnPtr conn) {
    conn->set_message_callback([&](std::string_view data) {
      client.send(data);
      client.close();
    });
    conn->set_close_callback([&](int) {
      client.close();
    });

    client.send("hello", 5);
  });

  client.set_error_user_callback([&](int) {
    client.close();
  });

  if (!client.connect()) {
    return 1;
  }

  client.run();
  return 0;
}
```

## Runtime Tuning

The following recommendations come from local loopback benchmarks on WSL2 Ubuntu 20.04 with `g++-13` and should be treated as tuning guidance, not as a hard rule for every deployment.

- Small-packet, high-frequency workloads should start with `1 poll`, then benchmark before increasing `thread_num`.
- On the local 64B benchmark, `1 poll` was faster than `8 poll` in both drain and echo scenarios:
  - `drain`: about `2.40M msg/s` vs `1.89M msg/s`
  - `echo`: about `345K req/s` vs `130K req/s`
- The likely reason is that for small packets, the extra cross-thread dispatch and wakeup costs can exceed the gains from additional polls.
- For low-to-medium connection counts, loopback testing, RPC-style request/response, or other latency-sensitive small-message traffic, prefer a single poll first.
- For larger payloads, higher connection counts, or real NIC traffic, do not assume more polls are always better or always worse. Benchmark `thread_num=1` against a few larger values on the target machine.
- This tuning choice should stay in application configuration. `cxpnet` does not try to auto-detect packet size patterns and switch poll strategies at runtime.

Typical starting points:

- Dedicated network thread: `set_thread_num(1)` + `start(RunningMode::kOnePollPerThread)` + `run()`
- Application-managed main loop: `start(RunningMode::kAllOneThread)` + repeated `poll()`
