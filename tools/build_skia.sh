#!/bin/bash
set -e

# Directory of this script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$DIR/.."
SKIA_DIR="$PROJECT_ROOT/third_party/skia"
BUILD_DIR="$PROJECT_ROOT/build/skia"

echo "Building Skia in $BUILD_DIR..."

if [ ! -d "$SKIA_DIR/include" ]; then
    echo "Error: Skia source not found at $SKIA_DIR"
    exit 1
fi

# 1. Sync dependencies
echo "Syncing Skia dependencies..."
cd "$SKIA_DIR"
python3 tools/git-sync-deps

# 2. Fetch GN if needed
if [ ! -f "bin/gn" ]; then
    echo "Fetching GN..."
    bin/fetch-gn
fi

# 3. Generate build files
# Common args for static release build
# We explicitly disable system libraries to ensure hermetic build
ARGS="is_official_build=true \
skia_use_system_expat=false \
skia_use_system_icu=false \
skia_use_system_libjpeg_turbo=false \
skia_use_system_libpng=false \
skia_use_system_libwebp=false \
skia_use_system_zlib=false \
skia_use_system_harfbuzz=false \
skia_use_metal=true \
skia_use_gl=true \
extra_cflags=[\"-frtti\"]"

# Note: -frtti is often needed if linking against RTTI-enabled code, though Skia defaults to -fno-rtti.
# If your project uses RTTI (standard C++), linking against no-rtti Skia can cause warning/issues, but usually fine for C API.
# Skia is C++, so we might need it.

echo "Generating build files with args: $ARGS"
bin/gn gen "$BUILD_DIR" --args="$ARGS"

# 4. Build
echo "Compiling Skia..."
ninja -C "$BUILD_DIR"

echo "Skia build complete."
echo "Library: $BUILD_DIR/libskia.a"
