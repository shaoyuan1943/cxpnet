#!/bin/bash

set -e

BUILD_ROOT="build"
BUILD_TYPE="Release"
BUILD_TYPE_DIR="release"
OUTPUT_SUFFIX=""
EXAMPLES_DIR="examples"
EXAMPLE_TARGETS=(
  echo_server
  echo_client
  manual_conn_client
  all_one_thread_server
  http_server
  http_client
  file_server
  file_client
  graceful_shutdown_server
  timer_poll
)

usage() {
  echo "Usage: $0 [debug|release] {clean|lib|examples|all|example_name}"
  echo
  echo "Actions:"
  echo "  clean          Recreate the selected build directory"
  echo "  lib            Build only the cxpnet library"
  echo "  examples       Build all example targets"
  echo "  all            Build the whole source tree"
  echo "  example_name   Build one example target, such as echo_server"
}

if [ "$(uname -s)" != "Darwin" ]; then
  echo "Error: build_macos.sh is only for macOS. Use build_linux.sh on Linux."
  exit 1
fi

if command -v clang++ >/dev/null 2>&1; then
  export CXX=clang++
  echo "Using clang++"
elif command -v g++ >/dev/null 2>&1; then
  export CXX=g++
  echo "Using g++"
else
  echo "Error: no C++ compiler found"
  exit 1
fi

if [ $# -eq 0 ]; then
  usage
  exit 1
fi

case "$1" in
  debug)
    BUILD_TYPE="Debug"
    BUILD_TYPE_DIR="debug"
    OUTPUT_SUFFIX="d"
    shift
    ;;
  release)
    BUILD_TYPE="Release"
    BUILD_TYPE_DIR="release"
    OUTPUT_SUFFIX=""
    shift
    ;;
esac

if [ $# -eq 0 ]; then
  usage
  exit 1
fi

BUILD_DIR="$BUILD_ROOT/$BUILD_TYPE_DIR"
ACTION=$1

example_exists() {
  local example_name=$1
  [ -f "$EXAMPLES_DIR/$example_name/CMakeLists.txt" ]
}

example_binary_path() {
  local example_name=$1
  echo "$BUILD_DIR/$EXAMPLES_DIR/$example_name/${example_name}${OUTPUT_SUFFIX}"
}

print_example_path() {
  local example_name=$1
  echo "  $(example_binary_path "$example_name")"
}

parallel_jobs() {
  sysctl -n hw.ncpu
}

ensure_cmake_project() {
  if [ ! -f "$BUILD_DIR/Makefile" ]; then
    echo "Build directory or Makefile not found. Generating CMake project..."
    mkdir -p "$BUILD_DIR"
  fi

  echo "Updating CMake project..."
  cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15
}

case "$ACTION" in
  clean)
    echo "Cleaning $BUILD_TYPE build directory..."
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    echo "Generating CMake project..."
    cmake -S . -B "$BUILD_DIR" \
      -DCMAKE_CXX_COMPILER="$CXX" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15
    echo "Clean and CMake generation completed."
    ;;

  lib)
    ensure_cmake_project
    echo "Building cxpnet library ($BUILD_TYPE)..."
    cmake --build "$BUILD_DIR" --target cxpnet -j"$(parallel_jobs)"
    echo "Library built successfully."
    ;;

  examples)
    ensure_cmake_project
    echo "Building all examples ($BUILD_TYPE)..."
    for example_name in "${EXAMPLE_TARGETS[@]}"; do
      cmake --build "$BUILD_DIR" --target "$example_name" -j"$(parallel_jobs)"
    done
    echo "All examples built successfully. Binaries:"
    for example_name in "${EXAMPLE_TARGETS[@]}"; do
      print_example_path "$example_name"
    done
    ;;

  all)
    ensure_cmake_project
    echo "Building cxpnet library and all examples ($BUILD_TYPE)..."
    cmake --build "$BUILD_DIR" -j"$(parallel_jobs)"
    echo "Build completed. Example binaries:"
    for example_name in "${EXAMPLE_TARGETS[@]}"; do
      print_example_path "$example_name"
    done
    ;;

  *)
    EXAMPLE_NAME=$ACTION

    if ! example_exists "$EXAMPLE_NAME"; then
      echo "Error: example '$EXAMPLE_NAME' not found in $EXAMPLES_DIR/"
      usage
      exit 1
    fi

    ensure_cmake_project
    echo "Building example: $EXAMPLE_NAME ($BUILD_TYPE)"
    cmake --build "$BUILD_DIR" --target "$EXAMPLE_NAME" -j"$(parallel_jobs)"
    echo "Example '$EXAMPLE_NAME' built successfully."
    echo "Binary: $(example_binary_path "$EXAMPLE_NAME")"
    ;;
esac

exit 0
