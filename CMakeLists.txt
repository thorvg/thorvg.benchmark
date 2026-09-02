cmake_minimum_required(VERSION 3.20)
project(vgbench LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep the normal build focused on ThorVG. Competitor adapters are opt-in,
# and each switch can be configured without discovering unrelated SDKs.
option(VGBENCH_ENABLE_SKIA "Build the Skia adapters" OFF)
option(VGBENCH_ENABLE_THORVG "Build the ThorVG adapters" ON)
option(VGBENCH_ENABLE_NANOVG "Build the NanoVG/OpenGL adapters" OFF)
option(VGBENCH_BUILD_THORVG_FROM_SOURCE
  "Build ThorVG from source instead of using pkg-config" OFF)

# Compatibility with the repository's previous ThorVG source-build flag.
if(DEFINED RECTBENCH_BUILD_THORVG)
  set(VGBENCH_BUILD_THORVG_FROM_SOURCE "${RECTBENCH_BUILD_THORVG}")
endif()

set(VGBENCH_CPP_ENGINES OFF)
if(VGBENCH_ENABLE_SKIA OR VGBENCH_ENABLE_THORVG OR VGBENCH_ENABLE_NANOVG)
  set(VGBENCH_CPP_ENGINES ON)
endif()

set(VGBENCH_WORKLOADS
  rect circle stroke image lineargradient radialgradient strokerect
)

add_library(vgbench_common INTERFACE)
target_include_directories(vgbench_common INTERFACE
  "${CMAKE_SOURCE_DIR}/src/common"
)
target_compile_definitions(vgbench_common INTERFACE
  VGBENCH_BUILD_TYPE="$<CONFIG>"
)

if(VGBENCH_CPP_ENGINES)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
  endif()
  if(SDL2_FOUND)
    set(VGBENCH_SDL_TARGET PkgConfig::SDL2)
  else()
    find_package(SDL2 CONFIG REQUIRED)
    if(TARGET SDL2::SDL2)
      set(VGBENCH_SDL_TARGET SDL2::SDL2)
    elseif(TARGET SDL2::SDL2-static)
      set(VGBENCH_SDL_TARGET SDL2::SDL2-static)
    else()
      message(FATAL_ERROR "SDL2 was found but did not provide an SDL2 target")
    endif()
  endif()
  find_package(OpenGL REQUIRED)
endif()

if(APPLE)
  set(VGBENCH_SKIA_FRAMEWORKS
    "-framework CoreFoundation" "-framework CoreGraphics"
    "-framework CoreText" "-framework CoreServices" "-framework Metal"
    "-framework IOKit" "-framework AppKit" "-framework Foundation"
  )
  set(VGBENCH_THORVG_FRAMEWORKS
    "-framework CoreGraphics" "-framework Metal" "-framework QuartzCore"
    "-framework Foundation" "-framework AppKit"
  )
endif()

if(VGBENCH_ENABLE_SKIA)
  find_package(unofficial-skia CONFIG QUIET)
  if(NOT unofficial-skia_FOUND)
    message(FATAL_ERROR
      "Skia not found. Install vcpkg skia or configure with "
      "-DVGBENCH_ENABLE_SKIA=OFF when selecting another engine.")
  endif()
  set(VGBENCH_SKIA_VERSION "unknown" CACHE STRING "Skia version metadata")
  set(VGBENCH_SKIA_REVISION "unknown" CACHE STRING "Skia revision metadata")

  function(add_skia_benchmark WORKLOAD)
    set(TARGET_NAME "${WORKLOAD}bench_skia_sdl")
    add_executable(${TARGET_NAME} "src/skia/${TARGET_NAME}.cpp")
    target_compile_definitions(${TARGET_NAME} PRIVATE
      VGBENCH_SKIA_VERSION="${VGBENCH_SKIA_VERSION}"
      VGBENCH_SKIA_REVISION="${VGBENCH_SKIA_REVISION}"
    )
    target_link_libraries(${TARGET_NAME} PRIVATE
      vgbench_common unofficial::skia::skia ${VGBENCH_SDL_TARGET}
      OpenGL::GL ${VGBENCH_SKIA_FRAMEWORKS}
    )
  endfunction()
  foreach(WORKLOAD IN LISTS VGBENCH_WORKLOADS)
    add_skia_benchmark(${WORKLOAD})
  endforeach()
endif()

if(VGBENCH_ENABLE_THORVG)
  if(NOT PkgConfig_FOUND)
    find_package(PkgConfig REQUIRED)
  endif()
  pkg_check_modules(WGPU QUIET IMPORTED_TARGET wgpu_native)
  if(WGPU_FOUND)
    set(VGBENCH_WGPU_TARGET PkgConfig::WGPU)
  elseif(WGPU_LIB AND WGPU_INCLUDE)
    add_library(vgbench_wgpu_native UNKNOWN IMPORTED)
    set_target_properties(vgbench_wgpu_native PROPERTIES
      IMPORTED_LOCATION "${WGPU_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${WGPU_INCLUDE}"
    )
    set(VGBENCH_WGPU_TARGET vgbench_wgpu_native)
  else()
    message(FATAL_ERROR
      "wgpu-native is required for ThorVG. Provide its pkg-config file or "
      "configure WGPU_LIB and WGPU_INCLUDE.")
  endif()

  if(VGBENCH_BUILD_THORVG_FROM_SOURCE)
    include(ExternalProject)
    set(VGBENCH_THORVG_INSTALL_DIR "${CMAKE_BINARY_DIR}/thorvg-install")
    set(VGBENCH_THORVG_LIBRARY_DIR "${VGBENCH_THORVG_INSTALL_DIR}/lib")
    file(MAKE_DIRECTORY "${VGBENCH_THORVG_INSTALL_DIR}/include")
    ExternalProject_Add(thorvg_build
      GIT_REPOSITORY https://github.com/thorvg/thorvg.git
      GIT_TAG main
      GIT_SHALLOW TRUE
      PREFIX "${CMAKE_BINARY_DIR}/thorvg"
      CONFIGURE_COMMAND
        meson setup
          <SOURCE_DIR>/build
          <SOURCE_DIR>
          --prefix=${VGBENCH_THORVG_INSTALL_DIR}
          --libdir=lib
          --buildtype=release
          -Ddefault_library=shared
          -Dbindings=cpp
          -Dloaders=all
          -Dsavers=all
          -Dengines=all
      BUILD_COMMAND meson compile -C <SOURCE_DIR>/build
      INSTALL_COMMAND meson install -C <SOURCE_DIR>/build
      BUILD_BYPRODUCTS
        "${VGBENCH_THORVG_LIBRARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}thorvg${CMAKE_SHARED_LIBRARY_SUFFIX}"
    )

    add_library(vgbench_thorvg UNKNOWN IMPORTED GLOBAL)
    set_target_properties(vgbench_thorvg PROPERTIES
      IMPORTED_LOCATION
        "${VGBENCH_THORVG_LIBRARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}thorvg${CMAKE_SHARED_LIBRARY_SUFFIX}"
      INTERFACE_INCLUDE_DIRECTORIES "${VGBENCH_THORVG_INSTALL_DIR}/include"
    )
    add_dependencies(vgbench_thorvg thorvg_build)
    set(VGBENCH_THORVG_TARGET vgbench_thorvg)
    set(VGBENCH_THORVG_DETECTED_VERSION "unknown")
  else()
    pkg_check_modules(THORVG REQUIRED IMPORTED_TARGET thorvg-1)
    set(VGBENCH_THORVG_TARGET PkgConfig::THORVG)
    set(VGBENCH_THORVG_DETECTED_VERSION "${THORVG_VERSION}")
  endif()

  set(VGBENCH_THORVG_VERSION "${VGBENCH_THORVG_DETECTED_VERSION}"
    CACHE STRING "ThorVG version metadata")
  set(VGBENCH_THORVG_REVISION "unknown"
    CACHE STRING "ThorVG revision metadata")

  function(add_thorvg_benchmark WORKLOAD)
    set(TARGET_NAME "${WORKLOAD}bench_thorvg_sdl")
    add_executable(${TARGET_NAME} "src/thorvg/${TARGET_NAME}.cpp")
    target_compile_definitions(${TARGET_NAME} PRIVATE
      VGBENCH_THORVG_VERSION="${VGBENCH_THORVG_VERSION}"
      VGBENCH_THORVG_REVISION="${VGBENCH_THORVG_REVISION}"
    )
    target_link_libraries(${TARGET_NAME} PRIVATE
      vgbench_common ${VGBENCH_THORVG_TARGET} ${VGBENCH_SDL_TARGET}
      OpenGL::GL ${VGBENCH_WGPU_TARGET} ${VGBENCH_THORVG_FRAMEWORKS}
    )
  endfunction()
  foreach(WORKLOAD IN LISTS VGBENCH_WORKLOADS)
    add_thorvg_benchmark(${WORKLOAD})
  endforeach()
endif()

if(VGBENCH_ENABLE_NANOVG)
  find_package(nanovg CONFIG REQUIRED)
  set(VGBENCH_NANOVG_VERSION "unknown"
    CACHE STRING "NanoVG version metadata")
  set(VGBENCH_NANOVG_REVISION "unknown"
    CACHE STRING "NanoVG revision metadata")
  add_library(vgbench_nanovg STATIC src/nanovg/nanovg_bench.cpp)
  target_compile_definitions(vgbench_nanovg PRIVATE
    VGBENCH_NANOVG_VERSION="${VGBENCH_NANOVG_VERSION}"
    VGBENCH_NANOVG_REVISION="${VGBENCH_NANOVG_REVISION}"
  )
  target_include_directories(vgbench_nanovg PUBLIC
    "${CMAKE_SOURCE_DIR}/src/nanovg"
  )
  target_link_libraries(vgbench_nanovg PUBLIC
    vgbench_common nanovg::nanovg ${VGBENCH_SDL_TARGET} OpenGL::GL
  )
  if(UNIX AND NOT APPLE)
    target_link_libraries(vgbench_nanovg PUBLIC m)
  endif()
  foreach(WORKLOAD IN LISTS VGBENCH_WORKLOADS)
    set(TARGET_NAME "${WORKLOAD}bench_nanovg_sdl")
    add_executable(${TARGET_NAME} "src/nanovg/${TARGET_NAME}.cpp")
    target_link_libraries(${TARGET_NAME} PRIVATE vgbench_nanovg)
  endforeach()
endif()

message(STATUS "=== vgbench configuration ===")
message(STATUS "Skia:       ${VGBENCH_ENABLE_SKIA}")
message(STATUS "ThorVG:     ${VGBENCH_ENABLE_THORVG}")
message(STATUS "NanoVG:     ${VGBENCH_ENABLE_NANOVG}")
