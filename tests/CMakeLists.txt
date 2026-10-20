find_package(Python3 REQUIRED COMPONENTS Interpreter)
option(RECTBENCH_TEST_COVERAGE "Instrument unit tests for gcov coverage (GCC)" OFF)

set(ADAPTER_HARNESS ${CMAKE_CURRENT_BINARY_DIR}/generated/adapter_harness.hpp)
add_custom_command(OUTPUT ${ADAPTER_HARNESS}
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/generate_adapter_harness.py
        ${PROJECT_SOURCE_DIR} ${ADAPTER_HARNESS}
    DEPENDS generate_adapter_harness.py
        ${PROJECT_SOURCE_DIR}/src/skia/skia_sdl_example.hpp
        ${PROJECT_SOURCE_DIR}/src/thorvg/tvg_sdl_example.hpp)

file(GLOB FACTORY_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/skia/*bench_skia_sdl.cpp
    ${PROJECT_SOURCE_DIR}/src/thorvg/*bench_thorvg_sdl.cpp)
set(FACTORY_HARNESS ${CMAKE_CURRENT_BINARY_DIR}/generated/factory_harness.hpp)
add_custom_command(OUTPUT ${FACTORY_HARNESS}
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/generate_factory_harness.py
        ${PROJECT_SOURCE_DIR} ${FACTORY_HARNESS}
    DEPENDS generate_factory_harness.py ${FACTORY_SOURCES})

foreach(suite workloads io runner sync factories)
    add_executable(test_${suite} test_${suite}.cpp)
    target_link_libraries(test_${suite} PRIVATE rectbench_common)
    target_include_directories(test_${suite} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    if(RECTBENCH_TEST_COVERAGE)
        if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            message(FATAL_ERROR "RECTBENCH_TEST_COVERAGE requires GCC/gcov")
        endif()
        target_compile_options(test_${suite} PRIVATE --coverage -O0 -g)
        target_link_options(test_${suite} PRIVATE --coverage)
    endif()
    add_test(NAME ${suite} COMMAND test_${suite})
endforeach()

target_sources(test_sync PRIVATE ${ADAPTER_HARNESS})
target_sources(test_factories PRIVATE ${FACTORY_HARNESS})

add_test(NAME cli_help COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_help.py
    $<TARGET_FILE:test_io> --help)
add_test(NAME cli_help_short COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_help.py
    $<TARGET_FILE:test_io> -h)
add_test(NAME tools COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
    -s ${CMAKE_CURRENT_SOURCE_DIR} -p test_tools.py)
