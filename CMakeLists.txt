# cxpnet/CMakeLists.txt

cmake_minimum_required(VERSION 3.23)
project(cxpnet LANGUAGES CXX)

add_library(cxpnet INTERFACE)
add_library(cxpnet::cxpnet ALIAS cxpnet)

# --- 指定 C++ 标准要求 ---
# 任何链接到 cxpnet 的项目都将被要求使用 C++20。
# CMake 会自动为用户的编译器添加 -std=c++20 标志。
target_compile_features(cxpnet INTERFACE cxx_std_20)

# --- 指定 Include 目录 ---
# 你的头文件直接在项目根目录下，所以我们指定 CMAKE_CURRENT_SOURCE_DIR。
# 当用户链接 cxpnet 时，这个目录会自动被添加到他们的 include 路径中，
# 让他们可以方便地使用 #include "cxpnet/YourHeader.h"。
target_include_directories(cxpnet
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# --- （可选）指定其他依赖 ---
# 如果你的头文件本身依赖于其他库（比如spdlog），可以在这里声明。
# 示例：
# find_package(spdlog REQUIRED)
# target_link_libraries(cxpnet INTERFACE spdlog::spdlog)

# 添加示例和测试
# 这样你可以在本地构建和测试，但用户在集成时不会构建它们。
# 当此项目作为顶层项目构建时，默认构建测试。
if(PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR AND BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

# 示例通常总是构建（如果存在的话）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()