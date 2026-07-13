cmake_minimum_required(VERSION 3.20)
project(rectbench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(ExternalProject)

# =============================================================================
# Options
# =============================================================================

option(
    RECTBENCH_BUILD_THORVG
    "Build ThorVG from source instead of using pkg-config"
    OFF
)

# =============================================================================
# Find Dependencies
# =============================================================================

find_package(PkgConfig REQUIRED)

# -----------------------------------------------------------------------------
# Skia
# -----------------------------------------------------------------------------

find_package(unofficial-skia CONFIG QUIET)

if(unofficial-skia_FOUND)
    set(SKIA_LIBS unofficial::skia::skia)
else()
    message(FATAL_ERROR
        "Skia not found.\n"
        "Install it with:\n"
        "  ~/vcpkg/vcpkg install skia sdl2 --triplet arm64-osx\n"
        "Then configure CMake with:\n"
        "  -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake"
    )
endif()

# -----------------------------------------------------------------------------
# SDL2
# -----------------------------------------------------------------------------

pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)

if(SDL2_FOUND)
    set(SDL2_LIBS PkgConfig::SDL2)
else()
    find_package(SDL2 CONFIG REQUIRED)

    if(TARGET SDL2::SDL2)
        set(SDL2_LIBS SDL2::SDL2)
    elseif(TARGET SDL2::SDL2-static)
        set(SDL2_LIBS SDL2::SDL2-static)
    else()
        message(FATAL_ERROR "SDL2 target not found.")
    endif()
endif()

# -----------------------------------------------------------------------------
# WebGPU
# -----------------------------------------------------------------------------

pkg_check_modules(WGPU REQUIRED IMPORTED_TARGET wgpu_native)
set(WGPU_LIBS PkgConfig::WGPU)

# =============================================================================
# ThorVG
# =============================================================================

if(RECTBENCH_BUILD_THORVG)

    # Build ThorVG from source using Meson.

    set(THORVG_INSTALL_DIR "${CMAKE_BINARY_DIR}/thorvg-install")
    set(THORVG_LIBRARY_DIR "${THORVG_INSTALL_DIR}/lib")

    ExternalProject_Add(
        thorvg_build

        GIT_REPOSITORY https://github.com/thorvg/thorvg.git
        GIT_TAG main
        GIT_SHALLOW TRUE

        PREFIX "${CMAKE_BINARY_DIR}/thorvg"

        CONFIGURE_COMMAND
            meson setup
                <SOURCE_DIR>/build
                <SOURCE_DIR>
                --prefix=${THORVG_INSTALL_DIR}
                --libdir=lib
                --buildtype=release
                -Ddefault_library=shared
                -Dbindings=cpp
                -Dloaders=all
                -Dsavers=all

        BUILD_COMMAND
            meson compile -C <SOURCE_DIR>/build

        INSTALL_COMMAND
            meson install -C <SOURCE_DIR>/build

        BUILD_BYPRODUCTS
            "${THORVG_LIBRARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}thorvg${CMAKE_SHARED_LIBRARY_SUFFIX}"
    )

    add_library(rectbench_thorvg UNKNOWN IMPORTED GLOBAL)

    set_target_properties(rectbench_thorvg PROPERTIES
        IMPORTED_LOCATION
            "${THORVG_LIBRARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}thorvg${CMAKE_SHARED_LIBRARY_SUFFIX}"

        INTERFACE_INCLUDE_DIRECTORIES
            "${THORVG_INSTALL_DIR}/include"
    )

    add_dependencies(rectbench_thorvg thorvg_build)

    set(THORVG_TARGET rectbench_thorvg)

else()

    # Use an installed ThorVG package through pkg-config.
    #
    # If thorvg.pc is not installed in a standard location,
    # add its directory to PKG_CONFIG_PATH.
    #
    # Example:
    #
    # export PKG_CONFIG_PATH=/path/to/thorvg/lib/pkgconfig:$PKG_CONFIG_PATH
    #

    pkg_check_modules(THORVG REQUIRED IMPORTED_TARGET thorvg-1)

    set(THORVG_TARGET PkgConfig::THORVG)

endif()

# =============================================================================
# Platform Frameworks
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

    set(SKIA_FRAMEWORKS)
    set(THORVG_FRAMEWORKS)

endif()

# =============================================================================
# Common Library
# =============================================================================

add_library(rectbench_common INTERFACE)

target_include_directories(
    rectbench_common
    INTERFACE
        ${CMAKE_SOURCE_DIR}/src/common
)

# =============================================================================
# Helper Functions
# =============================================================================

find_package(OpenGL REQUIRED)

function(add_skia_benchmark NAME SOURCE)

    add_executable(${NAME} ${SOURCE})

    target_link_libraries(${NAME}
        PRIVATE
            rectbench_common
            ${SKIA_LIBS}
            ${SDL2_LIBS}
            ${SKIA_FRAMEWORKS}
            OpenGL::GL
    )

endfunction()

function(add_thorvg_benchmark NAME SOURCE)

    add_executable(${NAME} ${SOURCE})

    target_link_libraries(${NAME}
        PRIVATE
            rectbench_common
            ${THORVG_TARGET}
            ${SDL2_LIBS}
            ${WGPU_LIBS}
            ${THORVG_FRAMEWORKS}
    )

    if(RECTBENCH_BUILD_THORVG)
        add_dependencies(${NAME} thorvg_build)
    endif()

endfunction()

# =============================================================================
# Skia Benchmarks
# =============================================================================

add_skia_benchmark(rectbench_skia_sdl           src/skia/rectbench_skia_sdl.cpp)
add_skia_benchmark(circlebench_skia_sdl         src/skia/circlebench_skia_sdl.cpp)
add_skia_benchmark(strokebench_skia_sdl         src/skia/strokebench_skia_sdl.cpp)
add_skia_benchmark(imagebench_skia_sdl          src/skia/imagebench_skia_sdl.cpp)
add_skia_benchmark(lineargradientbench_skia_sdl src/skia/lineargradientbench_skia_sdl.cpp)
add_skia_benchmark(radialgradientbench_skia_sdl src/skia/radialgradientbench_skia_sdl.cpp)
add_skia_benchmark(strokerectbench_skia_sdl     src/skia/strokerectbench_skia_sdl.cpp)

# =============================================================================
# ThorVG Benchmarks
# =============================================================================

add_thorvg_benchmark(rectbench_thorvg_sdl           src/thorvg/rectbench_thorvg_sdl.cpp)
add_thorvg_benchmark(circlebench_thorvg_sdl         src/thorvg/circlebench_thorvg_sdl.cpp)
add_thorvg_benchmark(strokebench_thorvg_sdl         src/thorvg/strokebench_thorvg_sdl.cpp)
add_thorvg_benchmark(imagebench_thorvg_sdl          src/thorvg/imagebench_thorvg_sdl.cpp)
add_thorvg_benchmark(lineargradientbench_thorvg_sdl src/thorvg/lineargradientbench_thorvg_sdl.cpp)
add_thorvg_benchmark(radialgradientbench_thorvg_sdl src/thorvg/radialgradientbench_thorvg_sdl.cpp)
add_thorvg_benchmark(strokerectbench_thorvg_sdl     src/thorvg/strokerectbench_thorvg_sdl.cpp)

# =============================================================================
# Configuration Summary
# =============================================================================

message(STATUS "")
message(STATUS "========== Rectbench Configuration ==========")
message(STATUS "Skia        : ${SKIA_LIBS}")
message(STATUS "SDL2        : ${SDL2_LIBS}")
message(STATUS "WebGPU      : pkg-config (wgpu_native)")

if(RECTBENCH_BUILD_THORVG)
    message(STATUS "ThorVG      : source build")
    message(STATUS "Install Dir : ${THORVG_INSTALL_DIR}")
else()
    message(STATUS "ThorVG      : pkg-config")
    message(STATUS "Version     : ${THORVG_VERSION}")
endif()

message(STATUS "=============================================")