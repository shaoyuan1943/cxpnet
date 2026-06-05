# AGENTS.md

This file defines how coding agents should work in this repository. Follow it before making code changes.

## Project Goal

`cxpnet` is a simple, lightweight, high-performance C++20 Reactor network library for Linux epoll and macOS kqueue.

The project values:

- simple public APIs
- small implementation units
- platform-independent upper layers
- source-backed fixes, not guesses
- explicit build and regression verification

## Required First Reads

Before coding, read:

1. `README.md` for public usage and build expectations
2. The relevant source files and local `CMakeLists.txt` files for the area being changed
3. Relevant examples under `examples/`

Before implementing new functionality or changing behavior, search the repository for existing implementations and similar patterns.

Use `rg` or `rg --files` first when searching.

## Directory Structure

- `cxpnet/`: core library headers and source files.
- `examples/`: user-facing source-built example programs.
- `CMakeLists.txt`: top-level library, platform selection, install rules, and examples entry.
- `examples/CMakeLists.txt`: explicitly lists public example targets.
- `build_linux.sh`: Linux build script; should work with `debug|release` and target names.
- `build_macos.sh`: macOS build script; cannot be runtime-validated on WSL, but syntax and source logic still matter.
- `README.md`: public-facing usage, build, and tuning notes.

Do not create new top-level folders unless the user explicitly asks. Put core code under `cxpnet/` and user-facing examples under `examples/`. Temporary test harnesses should not live under `examples/` or be committed as public examples.

When adding or removing source files, check and update the relevant CMake files. Header install uses `install(DIRECTORY cxpnet/ ... PATTERN "*.h")`, but library compilation still needs new `.cc` files listed in the top-level `CMakeLists.txt`.

When adding an example, the example directory name and CMake target name should match. This keeps `build_linux.sh debug <example_name>` working.

## Core Architecture

The library uses the Reactor model:

```text
Server -> Acceptor -> IOEventPoll -> Poller -> Channel -> Conn
                                      |
                                      v
                                 TimerManager
```

Component responsibilities:

- `Server`: TCP server lifecycle, acceptor ownership, connection registry, stop/close coordination.
- `Acceptor`: listen socket and accept handling.
- `IOEventPoll`: event loop, timer manager, task dispatch, wakeup.
- `Poller`: platform-specific event multiplexer abstraction.
- `Channel`: one fd/channel event adapter.
- `Conn`: connection state, reads/writes, shutdown/close cleanup, client connect path.
- `TimerManager`: timers and close timeouts.

Keep platform-specific event constants inside the Poller/platform layer. Upper layers should use project event abstractions, not raw `EPOLL*` or `EVFILT_*` values.

Linux behavior can be runtime-tested in WSL Ubuntu 20.04. macOS/kqueue behavior usually needs source-level checks here unless the user provides a macOS environment.

## Lifecycle Rules

`Server` and `Conn` are one-shot objects by convention. After `shutdown()/close()` or a failed start/connect path, create a new object instead of trying to reuse the old one.

`Conn` and `Channel` resource changes must run on the owning poll thread. Operations that touch fd/channel/poller state should go through `IOEventPoll::run_in_poll()` or an equivalent owner-thread path.

`Conn::shutdown()` is graceful half-close. `Conn::close()` is immediate cleanup.

`Server::shutdown()` is graceful: stop accepting, ask connections to shutdown, wait for completion or timeout, then force close remaining connections.

`Server::close()` is immediate: force close remaining resources. Repeated `close()` should remain safe.

For `RunningMode::kAllOneThread`, shutdown progress is poll-driven. After shutdown starts, `Server::poll()` must keep running while `stopping_` is true so events, timers, and final cleanup can progress.

For `RunningMode::kOnePollPerThread`, do not apply guards that only make sense for another mode. Validate mode-specific requirements after selecting the mode.

## Coding Style

- Use C++20.
- Keep functions small and direct.
- Prefer the simplest implementation that satisfies the requirement.
- Do not add classes, enums, helpers, or state unless they remove real complexity.
- Do not duplicate existing code or split a one-bit difference into multiple abstractions.
- Do not refactor unrelated modules.
- Use 2 spaces for indentation.
- Class names use uppercase style, for example `Server`, `Conn`, `TimerManager`.
- Functions use lowercase with underscores, for example `set_thread_num`, `handle_read_event_`.
- Private functions end with `_`.
- Member variables end with `_`.
- Constants and enum values use `k` prefixes, for example `kConnected`, `kRead`.
- Use comments only when they clarify non-obvious lifecycle, threading, or platform behavior.

Public or widely included macros must use a `CXPNET_` prefix. Do not introduce bare macros such as `CHECK` or `LOG_DEBUG`.

Use `CXPNET_CHECK` for enforced checks. It is Debug assert / Release exception behavior. Do not reintroduce `ENSURE` or `ensure.h`.

## Build System Rules

The project requires a C++20 toolchain with `std::format` support.

On WSL Ubuntu 20.04, use `/usr/bin/g++-13`.

Use `<repo-root-path>` for the current checkout root. When running commands from WSL, convert the checkout path to the path visible inside WSL. For example, a repository on a Windows drive is usually visible as `/mnt/<drive-letter>/<path-to-repo>`. `/mnt/e` means the Windows `E:` drive as mounted by WSL; it is not a project constant.

```bash
cmake -S <repo-root-path> -B <repo-root-path>/build/tdd \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-13
cmake --build <repo-root-path>/build/tdd -j 4
```

Use `build_linux.sh` for script-level validation:

```bash
bash build_linux.sh debug <target>
bash build_linux.sh debug examples
bash build_linux.sh release <target>
bash build_linux.sh debug all
```

If a build script fails because a new target is not discovered, check whether CMake configuration is stale and whether the target is listed in `examples/CMakeLists.txt`.

Run shell syntax checks after modifying scripts:

```bash
bash -n build_linux.sh
bash -n build_macos.sh
```

Do not claim macOS runtime behavior is verified from WSL. State that macOS was source-checked only unless it was actually built on macOS.

## Verification Rules

Verify before reporting completion.

For core library changes, usually run:

```bash
cmake --build <repo-root-path>/build/tdd -j 4
```

For example changes, run the script-level all-example build and each relevant target:

```bash
bash build_linux.sh debug examples
bash build_linux.sh debug <example_name>
```

Then smoke-test every changed example. Server examples should be tested with their matching client or a simple local TCP/HTTP request and then shut down cleanly.

Always run:

```bash
git diff --check
```

WSL may print noisy localhost/NAT warnings. Treat them as environment noise unless the command exit code or test output indicates failure.

Line-ending warnings from Git are not automatically functional failures, but still run `diff --check` to catch real whitespace damage.

## Working Tree Rules

The working tree may already be dirty. Do not revert changes you did not make.

If unrelated files are modified, ignore them. If a file you need to touch already has user changes, read it carefully and make the smallest compatible edit.

Do not use `git reset --hard`, `git checkout --`, or destructive cleanup unless the user explicitly asks.

Generated binaries should stay under `build/<type>/examples/<name>/`, not under `examples/`.

## Response Expectations

When answering questions about code behavior, start from the exact code path and state flow. Do not guess from symptoms.

When reporting completed work, include:

- what changed
- which files are relevant
- which commands were run
- what passed or could not be run

Do not claim success without fresh verification.
