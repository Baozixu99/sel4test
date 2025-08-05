#!/bin/bash

# Build script for Phytium Pi seL4 test
# Usage: ./build-phytium-pi.sh

set -e

# Configuration
PLATFORM="phytium-pi"
ARCH="aarch64"
BUILD_DIR="build-phytium-pi"
SOURCE_DIR="$(pwd)"

echo "Building seL4 for Phytium Pi..."
echo "Platform: $PLATFORM"
echo "Architecture: $ARCH"
echo "Build directory: $BUILD_DIR"

# Clean previous build
if [ -d "$BUILD_DIR" ]; then
    echo "Cleaning previous build..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo "Configuring with CMake..."
cmake -DCMAKE_TOOLCHAIN_FILE="../kernel/gcc.cmake" \
      -DPLATFORM="$PLATFORM" \
      -DAARCH64=1 \
      -DKernelSel4Arch="aarch64" \
      -DSIMULATION=OFF \
      -DKernelVerificationBuild=OFF \
      -DKernelDebugBuild=ON \
      -DKernelPrinting=ON \
      -G Ninja \
      "$SOURCE_DIR"

# Build
echo "Building..."
ninja

echo "Build complete!"
echo "Image location: $BUILD_DIR/images/"
ls -la images/ || echo "Images directory not found"
