#!/bin/bash

set -e

BUILD_ROOT="build"
BUILD_TYPE="Release"
BUILD_TYPE_DIR="release"
OUTPUT_SUFFIX=""
EXAMPLES_DIR="examples"

if command -v g++-13 >/dev/null 2>&1; then
  export CXX=g++-13
  echo "Using g++-13"
elif command -v g++ >/dev/null 2>&1; then
  export CXX=g++
  echo "Using g++"
else
  echo "Error: no C++ compiler found"
  exit 1
fi

if [ $# -eq 0 ]; then
  echo "Usage: $0 [debug|release] {clean|all|example_name}"
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
  echo "Usage: $0 [debug|release] {clean|all|example_name}"
  exit 1
fi

BUILD_DIR="$BUILD_ROOT/$BUILD_TYPE_DIR"
ACTION=$1

ensure_cmake_project() {
  if [ ! -f "$BUILD_DIR/Makefile" ]; then
    echo "Build directory or Makefile not found. Generating CMake project..."
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cmake -S . -B "$BUILD_DIR" \
      -DCMAKE_CXX_COMPILER="$CXX" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
  fi
}

case $ACTION in
  clean)
    echo "Cleaning $BUILD_TYPE build directory..."
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    echo "Generating CMake project..."
    cmake -S . -B "$BUILD_DIR" \
      -DCMAKE_CXX_COMPILER="$CXX" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    echo "Clean and CMake generation completed."
    ;;

  all)
    ensure_cmake_project

    echo "Building cxpnet library and all examples ($BUILD_TYPE)..."
    cmake --build "$BUILD_DIR" -j"$(nproc)"
    echo "All examples built successfully."

    echo "Copying executables..."
    find "$BUILD_DIR/$EXAMPLES_DIR" -type f -executable | while read -r exe_path; do
      exe_name=$(basename "$exe_path")
      dest_dir="$EXAMPLES_DIR/$exe_name/"

      if [ -d "$dest_dir" ]; then
        echo "  - Copying $exe_name to $dest_dir"
        cp "$exe_path" "$dest_dir"
      fi
    done
    echo "Copying completed."
    ;;

  *)
    EXAMPLE_NAME=$ACTION

    if [ ! -d "$EXAMPLES_DIR/$EXAMPLE_NAME" ]; then
      echo "Error: example '$EXAMPLE_NAME' not found in $EXAMPLES_DIR/"
      exit 1
    fi

    ensure_cmake_project

    echo "Building example: $EXAMPLE_NAME ($BUILD_TYPE)"
    cmake --build "$BUILD_DIR" --target "$EXAMPLE_NAME" -j"$(nproc)"
    echo "Example '$EXAMPLE_NAME' built successfully."

    SOURCE_EXE="$BUILD_DIR/$EXAMPLES_DIR/$EXAMPLE_NAME/${EXAMPLE_NAME}${OUTPUT_SUFFIX}"
    DEST_DIR="$EXAMPLES_DIR/$EXAMPLE_NAME/"

    echo "Copying $EXAMPLE_NAME to $DEST_DIR..."
    cp "$SOURCE_EXE" "$DEST_DIR"
    echo "Copying completed."
    ;;
esac

exit 0
