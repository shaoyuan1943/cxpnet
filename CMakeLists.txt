cmake_minimum_required(VERSION 3.16)
project(cxpnet VERSION 1.0.0 LANGUAGES CXX)

include(CheckCXXSourceCompiles)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_DEBUG_POSTFIX d)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  add_compile_options(/W4)
endif()

check_cxx_source_compiles("
  #include <format>
  #include <string>
  int main() {
    std::string msg = std::format(\"{}\", 42);
    return msg == \"42\" ? 0 : 1;
  }
" CXP_HAS_STD_FORMAT)

if(NOT CXP_HAS_STD_FORMAT)
  message(FATAL_ERROR
    "cxpnet requires a C++20 toolchain with std::format support. "
    "On Ubuntu 20.04, configure CMake with -DCMAKE_CXX_COMPILER=/usr/bin/g++-13.")
endif()

# Platform detection
set(CXP_PLATFORM_LINUX OFF)
set(CXPNET_PLATFORM_WINDOWS OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CXP_PLATFORM_LINUX ON)
  message(STATUS "Platform: Linux (epoll)")
elseif(WIN32)
  set(CXPNET_PLATFORM_WINDOWS ON)
  message(STATUS "Platform: Windows (WSAPoll)")
else()
  message(FATAL_ERROR "Unsupported platform: cxpnet only supports Linux and Windows")
endif()

# Common source files
set(COMMON_SOURCES
  cxpnet/acceptor.cc
  cxpnet/buffer.h
  cxpnet/channel.cc
  cxpnet/conn.cc
  cxpnet/io_event_poll.cc
  cxpnet/poll_thread_pool.cc
  cxpnet/sample_client.cc
  cxpnet/server.cc
  cxpnet/timer.cc
)

# Platform-specific source files
set(PLATFORM_SOURCES)
if(CXP_PLATFORM_LINUX)
  list(APPEND PLATFORM_SOURCES
    cxpnet/platform_api_for_linux.cc
    cxpnet/poller_for_epoll.cc
  )
elseif(CXPNET_PLATFORM_WINDOWS)
  list(APPEND PLATFORM_SOURCES
    cxpnet/platform_api_for_windows.cc
    cxpnet/poller_for_wsapoll.cc
  )
endif()

# Create the library
add_library(cxpnet STATIC
  ${COMMON_SOURCES}
  ${PLATFORM_SOURCES}
)

# Create an alias target so that cxpnet can be used as cxpnet::cxpnet
add_library(cxpnet::cxpnet ALIAS cxpnet)
target_compile_features(cxpnet PUBLIC cxx_std_20)
if(MSVC)
  # Public headers use C++20 __VA_OPT__ and contain UTF-8 comments.
  target_compile_options(cxpnet PUBLIC /Zc:preprocessor /utf-8)
endif()

# Public usage is:
#   #include <cxpnet/cxpnet.h>
# or
#   #include "cxpnet/cxpnet.h"
target_include_directories(cxpnet PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
)

# Platform-specific definitions
if(CXP_PLATFORM_LINUX)
  target_compile_definitions(cxpnet PRIVATE CXP_PLATFORM_LINUX=1)
elseif(CXPNET_PLATFORM_WINDOWS)
  target_compile_definitions(cxpnet PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# Link libraries
if(CXPNET_PLATFORM_WINDOWS)
  target_link_libraries(cxpnet PUBLIC Ws2_32)
elseif(CXP_PLATFORM_LINUX)
  find_package(Threads REQUIRED)
  target_link_libraries(cxpnet PUBLIC Threads::Threads)
endif()

# Install targets
install(TARGETS cxpnet
  EXPORT cxpnetTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)

install(DIRECTORY cxpnet/
  DESTINATION include/cxpnet
  FILES_MATCHING PATTERN "*.h"
)

install(EXPORT cxpnetTargets
  FILE cxpnetConfig.cmake
  NAMESPACE cxpnet::
  DESTINATION lib/cmake/cxpnet
)

# Create examples
add_subdirectory(examples)
