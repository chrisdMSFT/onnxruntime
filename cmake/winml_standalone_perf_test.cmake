# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.

if(NOT onnxruntime_BUILD_WINML_STANDALONE_PERF_TEST)
  message(FATAL_ERROR "onnxruntime_BUILD_WINML_STANDALONE_PERF_TEST is OFF")
endif()

if(NOT WIN32)
  message(FATAL_ERROR "winml_standalone_perf_test is only supported on Windows")
endif()

if(NOT MSVC)
  message(FATAL_ERROR "winml_standalone_perf_test is only supported with MSVC")
endif()

if(NOT onnxruntime_BUILD_SHARED_LIB)
  message(FATAL_ERROR "winml_standalone_perf_test requires onnxruntime_BUILD_SHARED_LIB to be ON")
endif()

if(onnxruntime_USE_CUDA OR onnxruntime_USE_NV OR onnxruntime_USE_TENSORRT)
  message(FATAL_ERROR "Unexpected - CUDA/NV/TensorRT usage in winml_standalone_perf_test")
endif()

# [WinML Standalone] Use the WinML package from a local folder.
if(NOT WINML_PACKAGE_DIR)
  set(WINML_PACKAGE_DIR "${CMAKE_BINARY_DIR}/__nuget/Microsoft.Windows.AI.MachineLearning.2.6.74-rc")
endif()

if(NOT EXISTS "${WINML_PACKAGE_DIR}/build/cmake/microsoft.windows.ai.machinelearning-config.cmake")
  message(FATAL_ERROR
    "WinML package not found at: ${WINML_PACKAGE_DIR}\n"
    "Copy the WinML package folder into build/__nuget.")
endif()

set(_winml_nuget_location "${WINML_PACKAGE_DIR}")
set(microsoft.windows.ai.machinelearning_DIR "${_winml_nuget_location}/build/cmake" CACHE PATH "" FORCE)
find_package(microsoft.windows.ai.machinelearning CONFIG REQUIRED)

# WINML_BINARY_DIR: use the same platform-detection logic as the package config
if(CMAKE_GENERATOR MATCHES "^Visual Studio")
  set(_winml_platform ${CMAKE_GENERATOR_PLATFORM})
else()
  set(_winml_proc ${CMAKE_SYSTEM_PROCESSOR})
  if(_winml_proc STREQUAL "")
    set(_winml_proc ${CMAKE_HOST_PROCESSOR})
  endif()
  if(_winml_proc STREQUAL "AMD64")
    set(_winml_platform "x64")
  elseif(_winml_proc STREQUAL "ARM64")
    set(_winml_platform "arm64")
  endif()
endif()

set(WINML_BINARY_DIR "${_winml_nuget_location}/runtimes/win-${_winml_platform}/native")

if(NOT EXISTS "${WINML_BINARY_DIR}")
  message(FATAL_ERROR "WINML_BINARY_DIR does not exist: ${WINML_BINARY_DIR}")
endif()

# Source files
set(winml_standalone_perf_test_src_dir ${TEST_SRC_DIR}/perftest)

set(winml_standalone_perf_test_src_patterns
  "${winml_standalone_perf_test_src_dir}/*.cc"
  "${winml_standalone_perf_test_src_dir}/*.h")

list(APPEND winml_standalone_perf_test_src_patterns
  "${winml_standalone_perf_test_src_dir}/windows/*.cc"
  "${winml_standalone_perf_test_src_dir}/windows/*.h")

file(GLOB winml_standalone_perf_test_src CONFIGURE_DEPENDS
  ${winml_standalone_perf_test_src_patterns}
)

# EXE
onnxruntime_add_executable(winml_standalone_perf_test
  ${winml_standalone_perf_test_src}
  ${winml_standalone_perf_test_src_dir}/windows/app.manifest
  ${ONNXRUNTIME_ROOT}/core/platform/path_lib.cc
)

target_compile_options(winml_standalone_perf_test PRIVATE ${disabled_warnings})

target_include_directories(winml_standalone_perf_test PRIVATE ${onnx_test_runner_src_dir} ${ONNXRUNTIME_ROOT}
  ${onnxruntime_graph_header} ${onnxruntime_exec_src_dir}
  ${CMAKE_CURRENT_BINARY_DIR})

target_compile_definitions(winml_standalone_perf_test
  PRIVATE
  ORT_API_MANUAL_INIT
  BUILD_WINML_STANDALONE_PERF_TEST
)

# ORT_API_MANUAL_INIT must be consistent across all linked objects
target_compile_definitions(onnx_test_runner_common PRIVATE ORT_API_MANUAL_INIT)
target_compile_definitions(onnxruntime_test_utils PRIVATE ORT_API_MANUAL_INIT)

# ABSL_FLAGS_STRIP_NAMES
target_compile_definitions(winml_standalone_perf_test PRIVATE ABSL_FLAGS_STRIP_NAMES=0)

target_link_libraries(winml_standalone_perf_test
  PRIVATE
  onnx_test_runner_common
  onnxruntime_test_utils
  onnxruntime_common
  onnxruntime_flatbuffers
  onnx_test_data_proto
  absl::flags
  absl::flags_parse
  ${onnxruntime_EXTERNAL_LIBRARIES}

  Threads::Threads

  WindowsML::WindowsML
)

# Copy runtime DLLs to build output so the EXE can run in-place
add_custom_command(TARGET winml_standalone_perf_test POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${WINML_BINARY_DIR}/Microsoft.Windows.AI.MachineLearning.dll"
    "$<TARGET_FILE_DIR:winml_standalone_perf_test>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${WINML_BINARY_DIR}/onnxruntime.dll"
    "$<TARGET_FILE_DIR:winml_standalone_perf_test>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${WINML_BINARY_DIR}/DirectML.dll"
    "$<TARGET_FILE_DIR:winml_standalone_perf_test>"
  VERBATIM
  COMMENT "Copying WinML runtime DLLs (Microsoft.Windows.AI.MachineLearning.dll, onnxruntime.dll, DirectML.dll)"
)
