cmake_minimum_required(VERSION 3.20)
project(rectbench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(ExternalProject)

# =============================================================================
# Options
# =============================================================================
option(RECTBENCH_BUILD_THORVG "Build ThorVG from source" ON)

# =============================================================================
# Find Dependencies
# =============================================================================
find_package(PkgConfig REQUIRED)

# --- Skia (vcpkg) ---
find_package(unofficial-skia CONFIG QUIET)
if(unofficial-skia_FOUND)
    set(SKIA_LIBS unofficial::skia::skia)
else()
    message(FATAL_ERROR
        "Skia not found. Install via vcpkg:\n"
        "  ~/vcpkg/vcpkg install skia sdl2 --triplet arm64-osx\n"
        "Then configure with:\n"
        "  cmake -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake ...")
endif()

# --- SDL2 ---
pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
if(NOT SDL2_FOUND)
    find_package(SDL2 CONFIG REQUIRED)
    if(TARGET SDL2::SDL2)
        set(SDL2_LIBS SDL2::SDL2)
    else()
        message(FATAL_ERROR "SDL2 not found")
    endif()
else()
    set(SDL2_LIBS PkgConfig::SDL2)
endif()

# --- WebGPU (wgpu-native via pkg-config) ---
pkg_check_modules(WGPU REQUIRED IMPORTED_TARGET wgpu_native)
set(WGPU_LIBS PkgConfig::WGPU)

# --- ThorVG ---
set(DEPS_BUILD_DIR "${CMAKE_SOURCE_DIR}/build")
if(RECTBENCH_BUILD_THORVG)
    set(THORVG_LIB "${DEPS_BUILD_DIR}/thorvg/src/libthorvg-1.a")
    set(THORVG_INCLUDE "${CMAKE_SOURCE_DIR}/third_party/thorvg/inc")

    ExternalProject_Add(thorvg_build
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/thorvg"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND "${CMAKE_SOURCE_DIR}/tools/build_thorvg.sh"
        INSTALL_COMMAND ""
        BUILD_ALWAYS ON
        BUILD_BYPRODUCTS "${THORVG_LIB}"
    )
else()
    set(THORVG_INCLUDE "${CMAKE_SOURCE_DIR}/third_party/thorvg/include" CACHE PATH "ThorVG include directory")
    set(THORVG_LIB "${CMAKE_SOURCE_DIR}/third_party/thorvg/lib/macOS/libthorvg.a" CACHE FILEPATH "ThorVG library path")
    if(NOT EXISTS "${THORVG_LIB}")
        message(FATAL_ERROR "ThorVG library not found at ${THORVG_LIB}.")
    endif()
endif()

# --- Image libraries for ThorVG ---
pkg_check_modules(PNG IMPORTED_TARGET libpng)
pkg_check_modules(TURBOJPEG IMPORTED_TARGET libturbojpeg)
pkg_check_modules(WEBP IMPORTED_TARGET libwebp)

set(THORVG_IMAGE_LIBS "")
if(PNG_FOUND)
    list(APPEND THORVG_IMAGE_LIBS PkgConfig::PNG)
endif()
if(TURBOJPEG_FOUND)
    list(APPEND THORVG_IMAGE_LIBS PkgConfig::TURBOJPEG)
endif()
if(WEBP_FOUND)
    list(APPEND THORVG_IMAGE_LIBS PkgConfig::WEBP)
endif()

# --- OpenMP ---
find_package(OpenMP QUIET)
if(NOT OpenMP_CXX_FOUND AND APPLE)
    set(LIBOMP_PREFIX "/opt/homebrew/opt/libomp" CACHE PATH "libomp prefix (Homebrew)")
    if(EXISTS "${LIBOMP_PREFIX}/lib/libomp.dylib")
        set(LIBOMP_INCLUDE "${LIBOMP_PREFIX}/include")
        set(LIBOMP_LIB "${LIBOMP_PREFIX}/lib/libomp.dylib")
    endif()
endif()

# =============================================================================
# Framework Dependencies
# =============================================================================
if(APPLE)
    set(SKIA_FRAMEWORKS
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework CoreText"
        "-framework CoreServices"
        "-framework OpenGL"
        "-framework Metal"
        "-framework IOKit"
        "-framework AppKit"
        "-framework Foundation"
    )
    set(THORVG_FRAMEWORKS
        "-framework CoreGraphics"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Foundation"
        "-framework AppKit"
    )
else()
    set(SKIA_FRAMEWORKS "")
    set(THORVG_FRAMEWORKS "")
endif()

# =============================================================================
# Common header-only library
# =============================================================================
add_library(rectbench_common INTERFACE)
target_include_directories(rectbench_common INTERFACE ${CMAKE_SOURCE_DIR}/src/common)

# =============================================================================
# Helper function for Skia executables
# =============================================================================
function(add_skia_benchmark NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME} PRIVATE
        rectbench_common
        ${SKIA_LIBS}
        ${SDL2_LIBS}
        ${SKIA_FRAMEWORKS}
    )
endfunction()

# =============================================================================
# Helper function for ThorVG executables
# =============================================================================
function(add_thorvg_benchmark NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_include_directories(${NAME} PRIVATE ${THORVG_INCLUDE})
    target_link_libraries(${NAME} PRIVATE
        rectbench_common
        ${THORVG_LIB}
        ${SDL2_LIBS}
        ${THORVG_FRAMEWORKS}
        ${THORVG_IMAGE_LIBS}
        ${WGPU_LIBS}
    )
    if(RECTBENCH_BUILD_THORVG)
        add_dependencies(${NAME} thorvg_build)
    endif()
    # OpenMP
    if(OpenMP_CXX_FOUND)
        target_link_libraries(${NAME} PRIVATE OpenMP::OpenMP_CXX)
    elseif(DEFINED LIBOMP_LIB)
        target_include_directories(${NAME} PRIVATE ${LIBOMP_INCLUDE})
        target_link_libraries(${NAME} PRIVATE ${LIBOMP_LIB})
    endif()
endfunction()

# =============================================================================
# Skia Benchmarks
# =============================================================================
add_skia_benchmark(rectbench_skia_sdl src/skia/rectbench_skia_sdl.cpp)
add_skia_benchmark(circlebench_skia_sdl src/skia/circlebench_skia_sdl.cpp)
add_skia_benchmark(strokebench_skia_sdl src/skia/strokebench_skia_sdl.cpp)
add_skia_benchmark(imagebench_skia_sdl src/skia/imagebench_skia_sdl.cpp)
add_skia_benchmark(lineargradientbench_skia_sdl src/skia/lineargradientbench_skia_sdl.cpp)
add_skia_benchmark(radialgradientbench_skia_sdl src/skia/radialgradientbench_skia_sdl.cpp)
add_skia_benchmark(strokerectbench_skia_sdl src/skia/strokerectbench_skia_sdl.cpp)

# =============================================================================
# ThorVG Benchmarks
# =============================================================================
add_thorvg_benchmark(rectbench_thorvg_sdl src/thorvg/rectbench_thorvg_sdl.cpp)
add_thorvg_benchmark(circlebench_thorvg_sdl src/thorvg/circlebench_thorvg_sdl.cpp)
add_thorvg_benchmark(strokebench_thorvg_sdl src/thorvg/strokebench_thorvg_sdl.cpp)
add_thorvg_benchmark(imagebench_thorvg_sdl src/thorvg/imagebench_thorvg_sdl.cpp)
add_thorvg_benchmark(lineargradientbench_thorvg_sdl src/thorvg/lineargradientbench_thorvg_sdl.cpp)
add_thorvg_benchmark(radialgradientbench_thorvg_sdl src/thorvg/radialgradientbench_thorvg_sdl.cpp)
add_thorvg_benchmark(strokerectbench_thorvg_sdl src/thorvg/strokerectbench_thorvg_sdl.cpp)

# =============================================================================
# Configuration Summary
# =============================================================================
message(STATUS "=== Rectbench Configuration ===")
message(STATUS "Skia: vcpkg")
message(STATUS "ThorVG: ${THORVG_LIB}")
message(STATUS "WebGPU: pkg-config (wgpu_native)")
