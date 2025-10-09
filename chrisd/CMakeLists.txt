cmake_minimum_required(VERSION 3.20)
project(onnxruntime_perf_test_minimal LANGUAGES CXX)

# Build options
option(USE_WINAPPSDK_SELF_CONTAINED "Enable Windows App SDK self-contained" ON)

# Platform-specific configurations
if(NOT WIN32)
    message(FATAL_ERROR "WIN32 ONLY")
endif()

# Compiler requirements
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# NuGet package integration (simplified)
include(FetchContent)

FetchContent_Declare(
    CMakeNuGetPackage
    GIT_REPOSITORY https://github.com/mschofie/NuGetCMakePackage
    GIT_TAG 47f603ef27f876c9132db81ba3c2895b3059c90c
)
FetchContent_MakeAvailable(CMakeNuGetPackage)

# Find ONNX Runtime installation -  This comes from the ML package...
find_package(onnxruntime QUIET)
if(NOT onnxruntime_FOUND)
    find_library(ONNXRUNTIME_LIB onnxruntime
        PATHS ${ONNXRUNTIME_ROOT}/lib)
    find_path(ONNXRUNTIME_INCLUDE onnxruntime_cxx_api.h
        PATHS ${ONNXRUNTIME_ROOT}/include)
endif()

include(external/onnxruntime_external_deps.cmake)
include(external/abseil-cpp.cmake)

# Find essential third-party libraries
find_package(absl REQUIRED)
find_package(Protobuf REQUIRED)
find_package(flatbuffers REQUIRED)
find_package(nlohmann_json REQUIRED)

set(PLATFORM_LIBS shlwapi dbghelp advapi32)

if(USE_WINAPPSDK_SELF_CONTAINED)
    set(PLATFORM_DEFS MICROSOFT_WINDOWSAPPSDK_SELFCONTAINED=1)
endif()

# Define source directory
set(PERF_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime/test/perftest")

# Collect platform-agnostic sources
file(GLOB PERF_TEST_SOURCES
    "${PERF_TEST_DIR}/*.cc"
    "${PERF_TEST_DIR}/*.h")

# Add platform-specific sources
if(WIN32)
    file(GLOB PLATFORM_SOURCES
        "${PERF_TEST_DIR}/windows/*.cc"
        "${PERF_TEST_DIR}/windows/*.h")
else()
    file(GLOB PLATFORM_SOURCES
        "${PERF_TEST_DIR}/posix/*.cc"
        "${PERF_TEST_DIR}/posix/*.h")
endif()

# Essential ONNX Runtime sources
set(CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime/core/platform/path_lib.cc")

# Simplified version of post_build_runtime_dll_copy
macro(copy_runtime_dlls target_name)
    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${target_name}>
            $<TARGET_FILE_DIR:${target_name}>
            COMMAND_EXPAND_LISTS)
    endif()
endmacro()

# Simplified executable creation
function(add_ort_executable target_name)
    add_executable(${target_name} ${ARGN})
    if(WIN32)
        set_target_properties(${target_name} PROPERTIES
            WIN32_EXECUTABLE TRUE)
    endif()
endfunction()

# Create executable
add_ort_executable(onnxruntime_perf_test
    ${PERF_TEST_SOURCES}
    ${PLATFORM_SOURCES}
    ${CORE_SOURCES})

# Compiler definitions
target_compile_definitions(onnxruntime_perf_test PRIVATE
    ABSL_FLAGS_STRIP_NAMES=0
    ${PLATFORM_DEFS})

# Include directories
target_include_directories(onnxruntime_perf_test PRIVATE
    ${ONNXRUNTIME_INCLUDE}
    ${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime/test
    ${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime)

# Link libraries
target_link_libraries(onnxruntime_perf_test PRIVATE
    ${ONNXRUNTIME_LIB}
    absl::flags
    absl::flags_parse
    protobuf::libprotobuf
    flatbuffers::flatbuffers
    nlohmann_json::nlohmann_json
    ${PLATFORM_LIBS})

target_link_options(onnxruntime_perf_test PRIVATE
    "/DELAYLOAD:onnxruntime.dll")

# SelfContained
if(USE_WINAPPSDK_SELF_CONTAINED)
    target_link_libraries(onnxruntime_perf_test PRIVATE
        Microsoft.WindowsAppSDK.ML_SelfContained)
endif()

copy_runtime_dlls(onnxruntime_perf_test)
