# cxpnet
A simple, lightweight, cross-platform reactor network library

## Project Structure

- `cxpnet/` Library code.
- `examples/` Use examples.
- `build_linux.sh` Build script for Linux.

## How To Use

- Use `#include <cxpnet/cxpnet.h>` or `#include "cxpnet/cxpnet.h"` in user code.
- Linux builds require a toolchain with `std::format` support.
- On Ubuntu 20.04, configure CMake with `-DCMAKE_CXX_COMPILER=/usr/bin/g++-13`.
- `Server` and `Conn` should be treated as one-shot objects by convention. After `shutdown()/close()` or a failed connect/start path, create a new object instead of reusing the old one.

### 1. Use In CMake Project

If your project uses CMake, the simplest way is:

```cmake
add_subdirectory(path/to/cxpnet)
target_link_libraries(your_target PRIVATE cxpnet::cxpnet)
```

Then include:

```cpp
#include <cxpnet/cxpnet.h>
```

### 2. Build `libcxpnet.a`

If you want a prebuilt static library, build the `cxpnet` target first.

Release:

```bash
bash build_linux.sh release clean
bash build_linux.sh release all
```

This generates:

```text
build/release/libcxpnet.a
```

Debug:

```bash
bash build_linux.sh debug clean
bash build_linux.sh debug all
```

This generates:

```text
build/debug/libcxpnetd.a
```

You can also build only the library target:

```bash
cmake -S . -B build/release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-13
cmake --build build/release -j"$(nproc)" --target cxpnet
```

### 3. Minimal Server Example

```cpp
#include <cxpnet/cxpnet.h>

using namespace cxpnet;

int main() {
  Server server("127.0.0.1", 9090);
  server.set_thread_num(1);
  server.set_conn_user_callback([](ConnPtr conn) {
    conn->set_conn_user_callbacks(
        [conn](Buffer* buffer) {
          conn->send(buffer->peek(), buffer->readable_size());
          buffer->been_read_all();
        },
        [](int) {});
  });

  if (!server.start(RunningMode::kOnePollPerThread)) {
    return 1;
  }

  server.run();
  return 0;
}
```

## Runtime Tuning

The following recommendations come from local loopback benchmarks on `WSL2 Ubuntu 20.04` with `g++-13` and should be treated as tuning guidance, not as a hard rule for every deployment.

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

## How To Add Files

### Add Source Code File

- Create new file(.h or .cc) in `cxpnet/`.
- Update the top-level `CMakeLists.txt`, add new files to `add_library()`.
