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

关闭语义、退出路径和用户回调中的关闭规则，集中在下文「退出与关闭」章节。

`SampleClient` 是一个很小的单连接客户端封装。连接失败（错误回调触发）后会自动进入终态，`run()`/`poll()` 随之返回，不会空转。如果需要重连逻辑、连接池、协议客户端或其他高级行为，直接组合使用 `Conn` 和 `IOEventPoll`。

`Conn` 的读处理有两种回调形式：

- `set_message_callback(std::function<void(std::string_view)>)` 是高级形式。这个 view 只在回调期间有效；回调返回后，cxpnet 会消费当前所有可读字节。
- `set_message_callback(std::function<void(Buffer*)>)` 是低级形式。适合需要通过 `consume(n)` 或 `consume_all()` 做局部消费的协议解析器。

连接关闭事件请单独使用 `set_close_callback(...)`。如果消息处理完成后需要关闭连接，关闭操作应通过 `run_later_in_poll()` 延后执行，具体写法和原因见「退出与关闭」章节。

如果需要直接使用项目检查宏，请包含 `#include <cxpnet/check.h>` 并使用 `CXPNET_CHECK(condition, fmt, ...)`。Debug 下它会 assert；Release 下它会抛出 `std::runtime_error`。

## 退出与关闭

### 两种关闭语义

| 操作 | 语义 | 未发数据 | 收敛时限 |
| --- | --- | --- | --- |
| `shutdown()` | 优雅关闭：写缓冲冲刷完后发 FIN，期间仍可接收对端数据 | 完整送达 | `set_graceful_close_timeout(ms)`，默认 500ms；0 表示不设上限，超时强制关闭 |
| `close()` | 立即关闭：马上回收资源 | 丢弃 | 无 |

先 `shutdown()` 再 `close()` 是合法的升级路径（例如第一次 SIGTERM 优雅退出、第二次强制）。

### Conn 的退出路径

连接状态机：`kCreated → kConnecting → kConnected → kClosing → kClosed`。一条连接有四种退出方式：

1. **对端半关闭（自然路径）**：收到对端 FIN 后，先把已收数据交付消息回调，并继续发送应用在回调中产生的响应；写缓冲排空后自动完成关闭。这条路径没有超时上限，慢速但存活的传输会完整送达；只有显式 `shutdown()`（或被 `Server::shutdown()` 波及）后才受 graceful 超时约束。
2. **`shutdown()`（主动优雅关闭）**：冲刷写缓冲后发 FIN，期间仍可接收。对正处于对端 FIN 后冲刷中的连接调用 `shutdown()`，会为它补上硬期限；连接还在 `kConnecting` 时调用 `shutdown()` 等价于 `close()`。
3. **`close()`（主动立即关闭）**：丢弃未发数据，立即回收。
4. **错误路径**：读、写或连接事件出错时，连接直接进入终态，并以错误码触发关闭回调。

关闭完成（进入 `kClosed`）时：先从所属 `Server` 的连接注册表摘除，再以原因码触发用户 close 回调（`0` 表示正常关闭，`ETIMEDOUT` 表示 graceful 超时，其余为 errno）；随后 `Conn` 清空全部用户回调，打断可能的引用环。

`shutdown()/close()/send()` 都可以在任意线程调用；实际的 channel 注销、fd 关闭和回调触发总是在所属 poll 线程执行——从其他线程调用时经任务队列投递，因此**所属 poll 必须保持驱动，清理才会发生**。

### Server 的退出路径

- **`shutdown()`**：发起优雅关闭并立即返回，不等待收敛。停止 accept，逐连接发起 `shutdown()` 并应用 graceful 超时。可以在任意线程调用，包括用户回调；重复调用无效。观察进度用 `connection_count()`。
- **`close()`**：立即关闭。逐连接 `close()`，join poll 线程并停掉 main poll，返回时清理已完成，随后即可析构。重复调用安全。**禁止在 poll 线程（含任何用户回调执行期间）调用**——`close()` 会 join poll 线程，等于 join 自己，库以 `CXPNET_CHECK` fail-fast；析构同理（析构即 `close()`）。
- **acceptor 的摘除时机**：acceptor 的实际摘除总是由 main poll 的驱动线程执行（内部经任务队列投递，不阻塞调用方）：`run()` 阻塞驱动下在 `run()` 退出前完成；逐帧 `poll()` 驱动下在随后的 `poll()` 中完成，因此 `close()` 之后还应继续调用 `poll()` 直到 `connection_count()` 归零。
- **关闭进展依赖驱动**：无论哪种运行模式，关闭流程中投递的任务、连接清理和定时器都靠 poll 的驱动推进。`run()` 只在 `close()` 之后返回；用 `poll()` 逐帧驱动时，退出过程中必须持续调用。

典型退出序列：

```cpp
// run() 阻塞驱动（两种运行模式均适用）
// 网络线程：
server.run();                    // close() 之后返回
// 控制线程（如信号处理）：
server.shutdown();               // 可选：先优雅
while (server.connection_count() != 0) { /* 等待收敛或自行超时 */ }
server.close();                  // run() 随之返回
// run() 返回后析构 Server
```

```cpp
// poll() 逐帧驱动
server.shutdown();               // 决定退出时发起
while (server.connection_count() != 0) {
  server.poll();                 // 持续驱动直到收敛
  // ... 应用自己的帧逻辑
}
server.close();                  // 投递强制关闭任务
while (server.connection_count() != 0) {
  server.poll();                 // 继续驱动，排空 acceptor 摘除和连接清理
}
// 停止 poll()，析构 Server
```

### 用户回调中的关闭规则

所有用户回调（连接建立、消息、关闭、错误）都在某个 poll 线程上执行。`shutdown()/close()` 的合法调用场景一览：

| 操作 | 回调中同步调用 | 回调中异步调用 | 其他线程调用 |
| --- | --- | --- | --- |
| `Conn::shutdown()` / `Conn::close()` | ❌ 禁止 | ✅ 经 `run_later_in_poll()` | ✅ 任意线程直接调用 |
| `Server::shutdown()` | ✅ 允许 | ✅ 允许 | ✅ 任意线程直接调用 |
| `Server::close()` / 析构 `Server` | ❌ 禁止 | ❌ 禁止 | ✅ 仅控制线程 |

- `Conn` 在回调中禁止同步关闭：poll 线程内的调用会当场执行清理（注销 channel、重入触发 close 回调、清空用户回调），打断进行中的事件派发。
- `Server::shutdown()` 在回调中同步调用是安全的：它只发起关闭、立即返回，不 join、不等待；同步执行的清理不触碰正在派发的连接 channel。
- `Server::close()` 在回调中同步、异步都禁止：它要 join poll 线程，即使异步投递，任务最终仍在 poll 线程上执行，等于 join 自己，库以 `CXPNET_CHECK` fail-fast。

回调中涉及 `Server`/`Conn` 的关闭时遵循两条规则：

**规则一：对当前 `Conn`，不要同步调用 `shutdown()/close()`，用 `run_later_in_poll()` 延后到回调返回后执行。**

```cpp
std::weak_ptr<cxpnet::Conn> weak_conn = conn;
conn->set_message_callback([weak_conn](std::string_view data) {
  auto conn = weak_conn.lock();
  if (!conn) { return; }

  conn->send(data);                          // send / 读取 Buffer / 只读查询可以同步
  conn->run_later_in_poll([conn]() {         // 关闭操作延后执行
    conn->shutdown();
  });
});
```

原因：回调返回后框架还要继续完成本次事件派发，同步关闭会当场注销 channel、触发关闭回调并清空用户回调，打断进行中的派发流程。`run_later_in_poll()` 投递的是一次性任务，可以捕获 `ConnPtr`，任务执行并销毁后引用计数正常减一；而消息/关闭回调是长生命周期回调，应捕获 `weak_ptr`，避免 `Conn → callback → Conn` 的引用环。

**规则二：对 `Server`，回调里只允许 `shutdown()`；`close()` 和析构留给控制线程。**

```cpp
// 错误：在回调里 close() 或析构 Server → CXPNET_CHECK fail-fast（join 自己）
// 正确：
server.shutdown();   // 回调里发起优雅关闭，立即返回
// 控制线程随后观察 connection_count()，或直接 close() 收尾
```

`SampleClient` 的 `shutdown()/close()` 是对内部 `Conn` 同语义操作的封装：`shutdown()` 等连接优雅关闭完成后自动停掉事件循环，`close()` 立即停。在它的回调中调用这两个方法，同样要经 `conn->run_later_in_poll()` 延后（参见 `echo_client` 示例）。

独立使用 `Conn`（不经过 `Server`/`SampleClient`）时：在释放最后一个 `ConnPtr` 前调用 `close()`，并让所属 `IOEventPoll` 驱动完清理；`IOEventPoll` 的生命周期必须长于其管理的 `Conn`。

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
- `graceful_shutdown_server`：展示第一次 `SIGINT/SIGTERM` 触发 `Server::shutdown()`，第二次升级到 `Server::close()`。
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

#include <memory>
#include <string_view>

int main() {
  cxpnet::Server server("127.0.0.1", 9090);
  server.set_thread_num(1);
  server.set_conn_user_callback([](cxpnet::ConnPtr conn) {
    std::weak_ptr<cxpnet::Conn> weak_conn = conn;
    conn->set_message_callback([weak_conn](std::string_view data) {
      if (auto conn = weak_conn.lock()) {
        conn->send(data);
      }
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

#include <memory>
#include <string_view>

int main() {
  cxpnet::SampleClient client("127.0.0.1", 9090);
  client.set_conn_user_callback([&](cxpnet::ConnPtr conn) {
    std::weak_ptr<cxpnet::Conn> weak_conn = conn;
    conn->set_message_callback([&client, weak_conn](std::string_view data) {
      client.send(data);
      if (auto conn = weak_conn.lock()) {
        conn->run_later_in_poll([&client]() {
          client.close();
        });
      }
    });
    conn->set_close_callback([&client, weak_conn](int) {
      if (auto conn = weak_conn.lock()) {
        conn->run_later_in_poll([&client]() {
          client.close();
        });
      }
    });

    client.send("hello", 5);
  });

  client.set_error_user_callback([&](int) {
    if (auto conn = client.conn()) {
      conn->run_later_in_poll([&client]() {
        client.close();
      });
    }
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
- 单线程阻塞服务：`start(RunningMode::kAllOneThread)` + `run()`——accept 与连接 IO 全在调用线程，适合不想自建循环的小服务
- tick 循环宿主（如游戏服务端）：`set_thread_num(1)` + `start(RunningMode::kOnePollPerThread)` + 主循环逐帧调用 `poll()`——accept 由主循环泵动，连接 IO 由 1 条网络线程阻塞驱动。`run()`/`poll()` 在两种运行模式下都可用：二者只决定 main poll 的驱动方式（阻塞或单次），运行模式只决定连接分配到哪个 poll。
