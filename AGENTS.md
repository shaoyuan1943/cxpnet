# AGENTS.md

本文件定义编码代理在本仓库中的工作方式。修改代码前必须遵守本文件。

## 项目目标

`cxpnet` 是一个简单、轻量、高性能的 C++20 Reactor 网络库，Linux 使用 epoll，Windows 使用 WSAPoll。

本项目重视：

- 简单的公共 API
- 小而清晰的实现单元
- 平台无关的上层逻辑
- 基于源码的修复，而不是猜测
- 明确的构建和回归验证

## 必读文件

编码前，先阅读：

1. `README.md`，了解公共用法和构建预期
2. 与修改区域相关的源文件和本地 `CMakeLists.txt` 文件
3. `examples/` 下的相关示例

实现新功能或改变行为前，先在仓库中搜索已有实现和相似模式。

搜索时优先使用 `rg` 或 `rg --files`。

## 目录结构

- `cxpnet/`：核心库头文件和源文件。
- `examples/`：面向用户、从源码构建的示例程序。
- `CMakeLists.txt`：顶层库、平台选择、安装规则和 examples 入口。
- `examples/CMakeLists.txt`：显式列出公开示例目标。
- `build_linux.sh`：Linux 构建脚本；应支持 `debug|release` 和目标名。
- `build_windows.ps1`：Windows 构建脚本（VS2022 / x64）；不能在 WSL 上做运行时验证，但语法和源码逻辑仍然重要。
- `README.md`：面向用户的用法、构建和调优说明。

除非用户明确要求，不要创建新的顶层目录。核心代码放在 `cxpnet/` 下，面向用户的示例放在 `examples/` 下。临时测试工具或代码应放在 `tests/` 下，也不应作为公开示例提交。

新增或删除源文件时，检查并更新相关 CMake 文件。头文件安装使用 `install(DIRECTORY cxpnet/ ... PATTERN "*.h")`，但库编译仍需要在顶层 `CMakeLists.txt` 中列出新的 `.cc` 文件。

新增示例时，示例目录名和 CMake 目标名应保持一致。这样可以保证 `build_linux.sh debug <example_name>` 正常工作。

## 代码提交规则

本仓库使用GitHub代码托管。在提交代码时：

- 仅提交核心库代码、使用案例代码、相关编译脚本、以及`README.md`

不要提交：

- 临时测试工具或测试代码
- AGENTS.md
- 编译中间文件

## 核心架构

本库使用 Reactor 模型：

```text
Server -> Acceptor -> IOEventPoll -> Poller -> Channel -> Conn
                                      |
                                      v
                                 TimerManager
```

组件职责：

- `Server`：TCP 服务器生命周期、acceptor 所有权、连接注册表、停止/关闭协调。
- `Acceptor`：监听 socket 和 accept 处理。
- `IOEventPoll`：事件循环、定时器管理、任务派发、唤醒。
- `Poller`：平台相关的事件复用抽象。
- `Channel`：单个 fd/channel 的事件适配器。
- `Conn`：连接状态、读写、shutdown/close 清理、客户端 connect 路径。
- `TimerManager`：定时器和关闭超时。

平台相关的事件常量应保留在 Poller/平台层。上层应使用项目事件抽象，不要直接使用原始 `EPOLL*` 或 `POLL*` 值。

Linux 行为可以在 WSL Ubuntu 20.04 中做运行时测试。Windows/WSAPoll 行为通常只能在这里做源码级检查，除非确实在 Windows 上构建并运行过。

## 生命周期规则

按约定，`Server` 和 `Conn` 都是一次性对象。调用 `shutdown()/close()` 之后，或者 start/connect 路径失败之后，应创建新对象，不要尝试复用旧对象。

`Conn` 和 `Channel` 的资源变更必须在其所属 poll 线程上执行。触碰 fd/channel/poller 状态的操作应通过 `IOEventPoll::run_in_poll()` 或等价的 owner-thread 路径。

`Conn::shutdown()` 是优雅半关闭。`Conn::close()` 是立即清理。

`Server::shutdown()` 是发起优雅关闭：停止 accept，逐连接发起 shutdown，然后立即返回，不等待收敛。观察进度用 `connection_count()`。它可以在任意线程调用，包括用户回调。

`Server::close()` 是立即关闭：强制关闭剩余资源并 join poll 线程，返回时清理已完成。重复调用 `close()` 应保持安全。`close()` 和析构禁止在 poll 线程（含用户回调）中调用——join 自己会死锁，库用 `CXPNET_CHECK` fail-fast。

对于 `RunningMode::kAllOneThread`，shutdown 进展由 poll 驱动。shutdown 开始后（state 为 `kClosing`），`Server::poll()` 必须继续运行，让事件、定时器和连接清理能够推进；`close()` 之后（state 为 `kClosed`）亦然，`poll()` 会继续排空已投递的关闭任务，直到 `connection_count()` 归零，由调用方决定何时停止并析构。

Acceptor 的实际关闭总是由 main poll 的驱动线程执行：`Server::shutdown()/close()` 通过 `IOEventPoll::run_in_poll()` 投递关闭任务，不在调用线程上直接拆除 acceptor/channel。修改关闭路径时必须保持这个不变量，否则会和事件派发线程并发访问 poller 的 channel 注册表。

对于 `RunningMode::kOnePollPerThread`，不要套用只对另一种模式有意义的 guard。选择模式后，要验证该模式自身的要求。

## 编码风格

- 使用 C++20。
- 函数保持小而直接。
- 优先使用满足需求的最简单实现。
- 除非能消除真实复杂度，否则不要新增类、枚举、helper 或状态。
- 不要重复已有代码，也不要为一位差异拆出多个抽象。
- 不要重构无关模块。
- 使用 2 个空格缩进。
- 类名使用大写风格，例如 `Server`、`Conn`、`TimerManager`。
- 函数使用小写加下划线，例如 `set_max_connections`、`handle_read_event_`。
- 私有函数以 `_` 结尾。
- 成员变量以 `_` 结尾。
- 常量和枚举值使用 `k` 前缀，例如 `kConnected`、`kRead`。
- 只有在解释不明显的生命周期、线程或平台行为时才写注释。

公共或广泛包含的宏必须使用 `CXPNET_` 前缀。不要引入 `CHECK` 或 `LOG_DEBUG` 这样的裸宏。

强制检查使用 `CXPNET_CHECK`。它在 Debug 下是 assert，在 Release 下是 exception 行为。不要重新引入 `ENSURE` 或 `ensure.h`。

## 构建系统规则

本项目需要支持 `std::format` 的 C++20 工具链。

在 WSL Ubuntu 20.04 上，使用 `/usr/bin/g++-13`。

使用 `<repo-root-path>` 表示当前 checkout 根目录。从 WSL 运行命令时，把 checkout 路径转换为 WSL 中可见的路径。例如，Windows 磁盘上的仓库通常会显示为 `/mnt/<drive-letter>/<path-to-repo>`。`/mnt/e` 表示 Windows 的 `E:` 盘在 WSL 中的挂载点，不是项目常量。

```bash
cmake -S <repo-root-path> -B <repo-root-path>/build/tdd \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-13
cmake --build <repo-root-path>/build/tdd -j 4
```

使用 `build_linux.sh` 做脚本级验证：

```bash
bash build_linux.sh debug <target>
bash build_linux.sh debug examples
bash build_linux.sh release <target>
bash build_linux.sh debug all
```

如果构建脚本因为新目标未被发现而失败，检查 CMake 配置是否陈旧，以及该目标是否列在 `examples/CMakeLists.txt` 中。

Windows 侧使用 `build_windows.ps1`，参数顺序与 `build_linux.sh` 一致：

```powershell
.\build_windows.ps1 debug <target>
.\build_windows.ps1 debug examples
.\build_windows.ps1 release <target>
.\build_windows.ps1 debug all
```

它使用 VS2022 / x64 生成器，构建目录固定为 `build/windows`。

修改脚本后运行 shell 语法检查：

```bash
bash -n build_linux.sh
```

`build_windows.ps1` 没有等价的 bash 语法检查，只能在 Windows 上用 PowerShell 解析或实际执行验证。

不要声称 Windows 运行时行为已经在 WSL 中验证。除非确实在 Windows/MSVC 上构建并运行过，否则说明 Windows 只做了源码检查。

## 验证规则

汇报完成前先验证。

核心库修改通常运行：

```bash
cmake --build <repo-root-path>/build/tdd -j 4
```

示例修改运行脚本级全示例构建和每个相关目标：

```bash
bash build_linux.sh debug examples
bash build_linux.sh debug <example_name>
```

然后对每个被修改的示例做冒烟测试。服务器示例应使用对应客户端或简单的本地 TCP/HTTP 请求测试，然后干净关闭。

始终运行：

```bash
git diff --check
```

WSL 可能打印嘈杂的 localhost/NAT 警告。除非命令退出码或测试输出表明失败，否则把它当作环境噪声。

Git 的换行符警告不自动等同于功能失败，但仍要运行 `diff --check` 捕获真实的空白字符损坏。

## 工作树规则

工作树可能已经是 dirty。不要回滚不是你做的修改。

如果有无关文件被修改，忽略它们。如果你需要修改的文件已经有用户改动，仔细阅读，做最小且兼容的编辑。

除非用户明确要求，不要使用 `git reset --hard`、`git checkout --` 或破坏性清理。

生成的二进制文件应留在 `build/<type>/examples/<name>/` 下，不要放进 `examples/`。

## 回复预期

回答代码行为问题时，从精确代码路径和状态流开始。不要根据症状猜测。

汇报已完成工作时，包含：

- 改了什么
- 相关文件
- 运行了哪些命令
- 哪些通过了，或者哪些无法运行

没有新鲜验证，不要声称成功。

## 提交规则

在没有特别指出的情况下，仅以下目录的文件允许提交：

- 根目录下的文件
- cxpnet 目录下的文件
- examples 目录下的文件

禁止提交：

- ./AGENTS.md