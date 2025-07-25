#!/bin/bash

# 脚本使用方法:
# ./build_linux.sh clean            # 清理并重新生成 cmake 工程
# ./build_linux.sh all              # 编译所有示例
# ./build_linux.sh example_name     # 编译特定示例

set -e  # 遇到错误时退出

BUILD_DIR="build"
EXAMPLES_DIR="examples"

if [ $# -eq 0 ]; then
    echo "Usage: $0 {clean|all|example_name}"
    exit 1
fi

ACTION=$1

case $ACTION in
    clean)
        echo "Cleaning build directory..."
        rm -rf $BUILD_DIR
        mkdir $BUILD_DIR
        cd $BUILD_DIR
        echo "Generating cmake project..."
        cmake ..
        echo "Clean and cmake generation completed."
        ;;
        
    all)
        if [ ! -d $BUILD_DIR ]; then
            echo "Build directory not found. Generating cmake project..."
            mkdir $BUILD_DIR
            cd $BUILD_DIR
            cmake ..
        else
            cd $BUILD_DIR
        fi
        echo "Building all examples..."
        make
        echo "All examples built successfully."
        find "$EXAMPLES_DIR" -mindepth 1 -type d | while IFS= read -r EXAMPLE_NAME; do
            cp $EXAMPLES_DIR/$EXAMPLE_NAME/$EXAMPLE_NAME ../$EXAMPLES_DIR/$EXAMPLE_NAME/$EXAMPLE_NAME
        done
        ;;
        
    *)
        # 编译特定示例
        EXAMPLE_NAME=$ACTION
        
        # 检查示例是否存在
        if [ ! -d "$EXAMPLES_DIR/$EXAMPLE_NAME" ]; then
            echo "Example '$EXAMPLE_NAME' not found in $EXAMPLES_DIR/"
            exit 1
        fi
        
        if [ ! -d $BUILD_DIR ]; then
            echo "Build directory not found. Generating cmake project..."
            mkdir $BUILD_DIR
            cd $BUILD_DIR
            cmake ..
        else
            cd $BUILD_DIR
        fi
        
        echo "Building example: $EXAMPLE_NAME"
        make $EXAMPLE_NAME
        echo "Example '$EXAMPLE_NAME' built successfully."
        cp ./$EXAMPLES_DIR/$EXAMPLE_NAME/$EXAMPLE_NAME ../$EXAMPLES_DIR/$EXAMPLE_NAME/$EXAMPLE_NAME
        ;;
esac