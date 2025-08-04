# ./cxpnet/CMakeLists.txt

# 1. 查找源文件
# =================
# 通用源文件
file(GLOB COMMON_SOURCES
    "acceptor.cc"
    "channel.cc"
    "conn.cc"
    "connector.cc"
    "io_event_poll.cc"
    "poll_thread_pool.cc"
    "server.cc"
)

# 平台特定的源文件 (例如 Poller 实现)
set(PLATFORM_SOURCES "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Platform: Linux. Using epoll.")
    list(APPEND PLATFORM_SOURCES "poller_for_epoll.cc" "platform_api_for_linux.cc")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(STATUS "Platform: macOS. You need to implement kqueue poller.")
    # list(APPEND PLATFORM_SOURCES "poller_for_kqueue.cc")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(STATUS "Platform: Windows. You need to implement IOCP poller.")
    list(APPEND PLATFORM_SOURCES "platform_api_for_win.cc")
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()


# 2. 定义库目标
# =================
add_library(cxpnet STATIC ${COMMON_SOURCES} ${PLATFORM_SOURCES})
add_library(cxpnet::cxpnet ALIAS cxpnet)


# 3. 设置目标属性 (跨平台)
# =================
# 设置 Include 目录，让使用者可以用 #include "cxpnet/..."
target_include_directories(cxpnet PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# 设置平台和编译器特定的编译选项
target_compile_options(cxpnet PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -g -rdynamic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# 链接平台特定的依赖库
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(cxpnet PUBLIC ws2_32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Threads REQUIRED)
    target_link_libraries(cxpnet PUBLIC Threads::Threads)
endif()