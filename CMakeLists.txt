# ./CMakeLists.txt

cmake_minimum_required(VERSION 3.16)
project(cxpnet-project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 添加 cxpnet 库作为一个子目录
# 它会执行 cxpnet/CMakeLists.txt 并定义一个 cxpnet::cxpnet 目标
add_subdirectory(cxpnet)

# 添加 examples 目录，它会负责构建所有示例
add_subdirectory(examples)