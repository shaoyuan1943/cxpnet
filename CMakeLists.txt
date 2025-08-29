cmake_minimum_required(VERSION 3.20)
project(cxpnet VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Platform-specific source files
set(PLATFORM_SOURCES)
if(UNIX AND NOT APPLE)
  # Linux-specific files
  list(APPEND PLATFORM_SOURCES
    cxpnet/platform_api_for_linux.cc
    cxpnet/poller_for_epoll.cc
  )
endif()

# Common source files
set(COMMON_SOURCES
  cxpnet/acceptor.cc
  cxpnet/channel.cc
  cxpnet/conn.cc
  cxpnet/connector.cc
  cxpnet/io_event_poll.cc
  cxpnet/poll_thread_pool.cc
  cxpnet/server.cc
)

# Create the library
add_library(cxpnet STATIC
  ${COMMON_SOURCES}
  ${PLATFORM_SOURCES}
)

# Create an alias target so that cxpnet can be used as cxpnet::cxpnet
add_library(cxpnet::cxpnet ALIAS cxpnet)

# Set include directories
target_include_directories(cxpnet PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cxpnet>
  $<INSTALL_INTERFACE:include>
)

# Platform-specific definitions
if(UNIX AND NOT APPLE)
  target_compile_definitions(cxpnet PRIVATE LINUX)
endif()

# Link libraries
if(UNIX AND NOT APPLE)
  target_link_libraries(cxpnet PRIVATE pthread)
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