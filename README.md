# cxpnet

一个简单、轻量、跨平台的 C++20 Reactor 网络库，Linux 使用 epoll，macOS 使用 kqueue。

## 项目结构

- `cxpnet/`：库头文件和源文件。
- `examples/`：面向用户、从源码构建的示例。
- `CMakeLists.txt`：顶层 CMake 项目和安装规则。
- `build_linux.sh`：Linux 构建辅助脚本。
- `build_macos.sh`：macOS 构建辅助脚本。

## 从源码使用 cxpnet

推荐的源码构建方式，是把本仓库加入你的 CMake 构建，并链接导出的源码目标：

```cmake
add_subdirectory(path/to/cxpnet)
target_link_libraries(your_target PRIVATE cxpnet::cxpnet)
```

然后包含公共总入口头文件：

```cpp
#include <cxpnet/cxpnet.h>
```

Linux 构建需要支持 `std::format` 的 C++20 工具链。在 Ubuntu 20.04 上，用下面的方式配置 CMake：

```bash
cmake -S <repo-root-path> -B <repo-root-path>/build/debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-13
```

按约定，`Server` 和 `Conn` 都是一次性对象。调用 `shutdown()/close()` 之后，或者 connect/start 路径失败之后，应创建新对象，不要复用旧对象。

`SampleClient` 是一个很小的单连接客户端封装。如果需要重连逻辑、连接池、协议客户端或其他高级行为，直接组合使用 `Conn` 和 `IOEventPoll`。

`Conn` 的读处理有两种回调形式：

- `set_message_callback(std::function<void(std::string_view)>)` 是高级形式。这个 view 只在回调期间有效；回调返回后，cxpnet 会消费当前所有可读字节。
- `set_message_callback(std::function<void(Buffer*)>)` 是低级形式。适合需要通过 `been_read(n)` 或 `been_read_all()` 做局部消费的协议解析器。

连接关闭事件请单独使用 `set_close_callback(...)`。

如果需要直接使用项目检查宏，请包含 `#include <cxpnet/check.h>` 并使用 `CXPNET_CHECK(condition, fmt, ...)`。Debug 下它会 assert；Release 下它会抛出 `std::runtime_error`。

## 构建

Linux：

```bash
bash build_linux.sh debug clean
bash build_linux.sh debug lib
bash build_linux.sh debug examples
bash build_linux.sh debug all
bash build_linux.sh debug echo_server
```

macOS：

```bash
bash build_macos.sh debug clean
bash build_macos.sh debug lib
bash build_macos.sh debug examples
bash build_macos.sh debug all
bash build_macos.sh debug echo_server
```

构建脚本会从源码编译 cxpnet 和示例。

示例二进制路径：

```text
build/debug/examples/echo_server/echo_serverd
build/release/examples/echo_server/echo_server
```

## 示例

所有示例都链接本源码树里的 `cxpnet::cxpnet`。

- `echo_server`：使用 `Server` 的最小 TCP echo 服务器。
- `echo_client`：使用 `SampleClient` 的最小 echo 客户端。
- `manual_conn_client`：直接使用 `Conn` 和 `IOEventPoll` 的低级客户端。
- `all_one_thread_server`：由重复调用 `Server::poll()` 驱动的 echo 服务器。
- `http_server`：基于 TCP 回调的最小 HTTP 服务器。
- `http_client`：使用 `SampleClient` 的 HTTP GET 客户端。
- `file_server`：简单的 `GET <path>` 文件传输服务器。
- `file_client`：把响应体写入磁盘的文件传输客户端。
- `timer_poll`：`IOEventPoll` 上的定时器回调示例。

典型本地运行方式：

```bash
bash build_linux.sh debug examples

./build/debug/examples/echo_server/echo_serverd
./build/debug/examples/echo_client/echo_clientd 127.0.0.1 9090
```

HTTP：

```bash
./build/debug/examples/http_server/http_serverd 127.0.0.1 8080
./build/debug/examples/http_client/http_clientd 127.0.0.1 8080 /
```

文件传输：

```bash
./build/debug/examples/file_server/file_serverd 127.0.0.1 9094 /tmp
./build/debug/examples/file_client/file_clientd 127.0.0.1 9094 sample.txt downloaded_sample.txt
```

## 最小 Server 示例

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

## 最小 SampleClient 示例

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

## 运行时调优

下面的建议来自 WSL2 Ubuntu 20.04 上使用 `g++-13` 做的本地 loopback 基准测试。它们应该被当作调优参考，而不是所有部署环境的硬规则。

- 小包、高频负载应先从 `1 poll` 开始，然后基准测试后再决定是否增加 `thread_num`。
- 在本地 64B 基准中，`1 poll` 在 drain 和 echo 场景里都比 `8 poll` 更快：
  - `drain`：约 `2.40M msg/s`，对比 `1.89M msg/s`
  - `echo`：约 `345K req/s`，对比 `130K req/s`
- 可能原因是：对小包来说，额外的跨线程派发和唤醒成本可能超过增加 poll 带来的收益。
- 对低到中等连接数、loopback 测试、RPC 风格请求/响应，或其他对延迟敏感的小消息流量，优先尝试单 poll。
- 对更大 payload、更高连接数或真实网卡流量，不要假设更多 poll 一定更好或一定更差。应在目标机器上把 `thread_num=1` 和几个更大的值做基准对比。
- 这个调优选择应留在应用配置里。`cxpnet` 不会尝试在运行时自动检测包大小模式并切换 poll 策略。

典型起点：

- 专用网络线程：`set_thread_num(1)` + `start(RunningMode::kOnePollPerThread)` + `run()`
- 应用托管主循环：`start(RunningMode::kAllOneThread)` + 重复调用 `poll()`
