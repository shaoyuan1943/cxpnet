#!/bin/bash

# cxpnet macOS 编译脚本
# 脚本使用方法:
# ./build_macos.sh clean            # 清理并重新生成 cmake 工程
# ./build_macos.sh all              # 编译所有示例
# ./build_macos.sh example_name     # 编译特定示例

set -e  # 遇到错误时退出

# -- 配置 --
BUILD_DIR="build"
EXAMPLES_DIR="examples"

# -- 检测平台 --
if [ "$(uname -s)" != "Darwin" ]; then
    echo "错误: 此脚本仅用于 macOS 平台"
    echo "Linux 用户请使用 build_linux.sh"
    exit 1
fi

# -- 检测编译器 --
if command -v clang++ &> /dev/null; then
    export CXX=clang++
    echo "Using clang++"
elif command -v g++ &> /dev/null; then
    export CXX=g++
    echo "Using g++"
else
    echo "Error: No C++ compiler found"
    exit 1
fi

# -- 检查输入 --
if [ $# -eq 0 ]; then
    echo "错误: 请提供操作参数。"
    echo "用法: $0 {clean|all|example_name}"
    exit 1
fi

ACTION=$1

# --- CMake 生成函数 ---
ensure_cmake_project() {
    if [ ! -f "$BUILD_DIR/Makefile" ]; then
        echo "Build directory or Makefile not found. Generating cmake project..."
        rm -rf "$BUILD_DIR"
        mkdir -p "$BUILD_DIR"
        # macOS 特定 CMake 配置
        cmake -S . -B "$BUILD_DIR" \
            -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15
    fi
}

# --- 主逻辑 ---
case $ACTION in
    clean)
        echo "Cleaning build directory..."
        rm -rf "$BUILD_DIR"
        mkdir -p "$BUILD_DIR"
        echo "Generating cmake project for macOS..."
        cmake -S . -B "$BUILD_DIR" \
            -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15
        echo "Clean and cmake generation completed."
        ;;

    all)
        ensure_cmake_project

        echo "Building cxpnet library and all examples..."
        # macOS 使用 sysctl 获取 CPU 核心数
        make -C "$BUILD_DIR" -j$(sysctl -n hw.ncpu)
        echo "Build completed."

        echo "Copying executables..."
        # macOS 使用 -perm +111 检测可执行文件
        find "$BUILD_DIR" -path "./$EXAMPLES_DIR/*" -type f -perm +111 | while read -r exe_path; do
            exe_name=$(basename "$exe_path")
            dest_dir="$EXAMPLES_DIR/$exe_name/"
            echo "  - Copying $exe_name to $dest_dir"
            cp "$exe_path" "$dest_dir"
        done
        echo "Copying completed."
        ;;

    *)
        EXAMPLE_NAME=$ACTION

        if [ ! -d "$EXAMPLES_DIR/$EXAMPLE_NAME" ]; then
            echo "错误: Example '$EXAMPLE_NAME' not found in $EXAMPLES_DIR/"
            exit 1
        fi

        ensure_cmake_project

        echo "Building example: $EXAMPLE_NAME"
        make -C "$BUILD_DIR" "$EXAMPLE_NAME" -j$(sysctl -n hw.ncpu)
        echo "Example '$EXAMPLE_NAME' built successfully."

        SOURCE_EXE="$BUILD_DIR/$EXAMPLES_DIR/$EXAMPLE_NAME/$EXAMPLE_NAME"
        DEST_DIR="$EXAMPLES_DIR/$EXAMPLE_NAME/"

        echo "Copying $EXAMPLE_NAME to $DEST_DIR..."
        cp "$SOURCE_EXE" "$DEST_DIR"
        echo "Copying completed."
        ;;
esac

exit 0