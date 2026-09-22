cmake_minimum_required(VERSION 3.20)

project(SmoothDemon LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++ configuration
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use Release builds by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Dependencies
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Common compiler warnings/options
set(SMOOTHDEMON_COMPILE_OPTIONS
    -Wall
    -Wextra
    -Wpedantic
    -O2
)

# Core library
add_library(smoothdemon_core STATIC
    src/core_placeholder.cpp
    src/cpu.cpp
    src/memory.cpp
    src/process.cpp
    src/temperature.cpp
    src/gpu.cpp
    src/system_info.cpp
    src/benchmark/HighResTimer.cpp
    src/benchmark/FrameMetrics.cpp
    src/resource/IOPriority.cpp
    src/resource/ProcessScheduler.cpp
    src/resource/CpuAffinity.cpp
)

target_include_directories(smoothdemon_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(smoothdemon_core
    PRIVATE
        ${SMOOTHDEMON_COMPILE_OPTIONS}
)

target_link_libraries(smoothdemon_core
    PRIVATE
        Threads::Threads
)

# Capture dependencies
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(SDBUS REQUIRED IMPORTED_TARGET sdbus-c++)

# Capture library
add_library(smoothdemon_capture STATIC
    src/capture/PortalSession.cpp
    src/capture/PipeWireCapture.cpp
)

target_include_directories(smoothdemon_capture
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(smoothdemon_capture
    PRIVATE
        ${SMOOTHDEMON_COMPILE_OPTIONS}
)

target_link_libraries(smoothdemon_capture
    PUBLIC
        smoothdemon_core
        PkgConfig::PIPEWIRE
        PkgConfig::LIBDRM
        PkgConfig::SDBUS
)

# Main executable
add_executable(smoothdemon
    src/main.cpp
)

target_link_libraries(smoothdemon
    PRIVATE
        smoothdemon_core
        smoothdemon_capture
)

target_compile_options(smoothdemon
    PRIVATE
        ${SMOOTHDEMON_COMPILE_OPTIONS}
)

# Helper for creating test executables
function(smoothdemon_add_test name source)
    add_executable(${name} ${source})

    target_link_libraries(${name}
        PRIVATE
            smoothdemon_core
    )

    target_compile_options(${name}
        PRIVATE
            ${SMOOTHDEMON_COMPILE_OPTIONS}
    )

    set_target_properties(${name}
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    )
endfunction()

# Tests
smoothdemon_add_test(test_environment tests/test_environment.cpp)
smoothdemon_add_test(test_cpu tests/test_cpu.cpp)
smoothdemon_add_test(test_memory tests/test_memory.cpp)
smoothdemon_add_test(test_process tests/test_process.cpp)
smoothdemon_add_test(test_temperature tests/test_temperature.cpp)
smoothdemon_add_test(test_gpu tests/test_gpu.cpp)
smoothdemon_add_test(test_system_info tests/test_system_info.cpp)
smoothdemon_add_test(test_benchmark tests/test_benchmark.cpp)
smoothdemon_add_test(test_resource tests/test_resource.cpp)

# Capture test uses the additional capture dependencies
add_executable(test_capture tests/test_capture.cpp)

target_link_libraries(test_capture
    PRIVATE
        smoothdemon_capture
)

target_compile_options(test_capture
    PRIVATE
        ${SMOOTHDEMON_COMPILE_OPTIONS}
)

set_target_properties(test_capture
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# Reminding about the capability required by the GPU test
add_custom_command(TARGET smoothdemon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo
        "NOTE: run 'sudo setcap cap_sys_ptrace+ep build/smoothdemon build/tests/test_gpu'"
)
