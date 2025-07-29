#!/bin/bash

# 脚本使用方法:
# ./build_linux.sh clean            # 清理并重新生成 cmake 工程
# ./build_linux.sh all              # 编译所有示例
# ./build_linux.sh example_name     # 编译特定示例

set -e  # 遇到错误时退出

# -- 配置 --
BUILD_DIR="build"
EXAMPLES_DIR="examples"

# -- 检查输入 --
if [ $# -eq 0 ]; then
    echo "错误: 请提供操作参数。"
    echo "用法: $0 {clean|all|example_name}"
    exit 1
fi

ACTION=$1

# --- CMake 生成函数 ---
# 确保 build 目录存在并已生成 CMake 项目
ensure_cmake_project() {
    if [ ! -f "$BUILD_DIR/Makefile" ]; then
        echo "Build directory or Makefile not found. Generating cmake project..."
        rm -rf "$BUILD_DIR"
        mkdir -p "$BUILD_DIR"
        # 使用 -B 指定构建目录，-S 指定源目录，这是现代 CMake 的推荐用法
        cmake -S . -B "$BUILD_DIR"
    fi
}

# --- 主逻辑 ---
case $ACTION in
    clean)
        echo "Cleaning build directory..."
        rm -rf "$BUILD_DIR"
        mkdir -p "$BUILD_DIR"
        echo "Generating cmake project..."
        cmake -S . -B "$BUILD_DIR"
        echo "Clean and cmake generation completed."
        ;;
        
    all)
        ensure_cmake_project
        
        echo "Building all examples..."
        # 使用 make -C 指定构建目录，-j$(nproc) 使用所有CPU核心并行编译
        make -C "$BUILD_DIR" -j$(nproc)
        echo "All examples built successfully."

        echo "Copying executables..."
        # 查找 build/examples/ 下的所有可执行文件
        find "$BUILD_DIR" -path "./$EXAMPLES_DIR/*" -type f -executable | while read -r exe_path; do
            # 获取可执行文件名，例如 "async_client"
            exe_name=$(basename "$exe_path")
            # 获取目标目录，例如 "examples/async_client/"
            dest_dir="$EXAMPLES_DIR/$exe_name/"
            
            echo "  - Copying $exe_name to $dest_dir"
            # 拷贝可执行文件到其对应的源目录
            cp "$exe_path" "$dest_dir"
        done
        echo "Copying completed."
        ;;
        
    *)
        # 编译特定示例
        EXAMPLE_NAME=$ACTION
        
        # 检查示例是否存在
        if [ ! -d "$EXAMPLES_DIR/$EXAMPLE_NAME" ]; then
            echo "错误: Example '$EXAMPLE_NAME' not found in $EXAMPLES_DIR/"
            exit 1
        fi
        
        ensure_cmake_project
        
        echo "Building example: $EXAMPLE_NAME"
        # CMake 会为每个可执行文件目标生成一个同名的 make target
        make -C "$BUILD_DIR" "$EXAMPLE_NAME" -j$(nproc)
        echo "Example '$EXAMPLE_NAME' built successfully."
        
        # 定义源可执行文件路径和目标目录
        SOURCE_EXE="$BUILD_DIR/$EXAMPLES_DIR/$EXAMPLE_NAME/$EXAMPLE_NAME"
        DEST_DIR="$EXAMPLES_DIR/$EXAMPLE_NAME/"

        echo "Copying $EXAMPLE_NAME to $DEST_DIR..."
        cp "$SOURCE_EXE" "$DEST_DIR"
        echo "Copying completed."
        ;;
esac

exit 0