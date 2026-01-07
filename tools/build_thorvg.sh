#!/bin/bash
set -e

# Directory of this script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$DIR/.."
THORVG_DIR="$PROJECT_ROOT/third_party/thorvg"
BUILD_DIR="$PROJECT_ROOT/build/thorvg"

echo "Building ThorVG in $BUILD_DIR..."

if [ ! -d "$THORVG_DIR/src" ]; then
    echo "Error: ThorVG source not found at $THORVG_DIR"
    exit 1
fi

# Ensure meson and ninja are available
if ! command -v meson &> /dev/null; then
    echo "Error: meson not found"
    exit 1
fi

if ! command -v ninja &> /dev/null; then
    echo "Error: ninja not found"
    exit 1
fi

# Configure
# We enable all engines (SW, GL, WebGPU), disable partial rendering, and enable all savers.
# Loaders: all (or subset).
# Static library.

# Clean up if it exists but doesn't look like a meson build (to avoid "Directory not empty" error)
if [ -d "$BUILD_DIR" ] && [ ! -f "$BUILD_DIR/build.ninja" ]; then
    echo "Directory $BUILD_DIR exists but is not a build directory. Cleaning it..."
    rm -rf "$BUILD_DIR"
fi

if [ -f "$BUILD_DIR/build.ninja" ]; then
    echo "Reconfiguring existing build..."
    meson setup "$BUILD_DIR" "$THORVG_DIR" \
        --reconfigure \
        --default-library=static \
        --buildtype=release \
        -Dengines=all \
        -Dloaders=all \
        -Dpartial=false \
        -Dsavers=all
else
    echo "Setting up new build..."
    meson setup "$BUILD_DIR" "$THORVG_DIR" \
        --default-library=static \
        --buildtype=release \
        -Dengines=all \
        -Dloaders=all \
        -Dpartial=false \
        -Dsavers=all
fi

# Build
ninja -C "$BUILD_DIR"

echo "ThorVG build complete."
echo "Library: $BUILD_DIR/src/libthorvg.a"
