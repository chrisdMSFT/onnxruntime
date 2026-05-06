# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.
#
# standalone_winml_perf_test
# --------------------------
# Windows-only ONNX Runtime perf-test variant that loads ORT through the
# *standalone* Microsoft.Windows.AI.MachineLearning runtime via the flat-C
# WinMLEpCatalog* API (no Windows App SDK, no WinRT projection). The locally
# built onnxruntime.dll is deployed sibling to the exe and forwarded to via
# a hand-rolled OrtGetApiBase shim.
#
# See standalone_winml_perf_test_proposal.md for the design rationale.

if(NOT onnxruntime_BUILD_STANDALONE_WINML_PERF_TEST)
  message(FATAL_ERROR "onnxruntime_BUILD_STANDALONE_WINML_PERF_TEST is OFF")
endif()

if(NOT WIN32)
  message(FATAL_ERROR "standalone_winml_perf_test is only supported on Windows")
endif()

if(NOT MSVC)
  message(FATAL_ERROR "standalone_winml_perf_test is only supported with MSVC")
endif()

if(NOT onnxruntime_BUILD_SHARED_LIB)
  message(FATAL_ERROR "standalone_winml_perf_test requires onnxruntime_BUILD_SHARED_LIB to be ON")
endif()

if(onnxruntime_USE_CUDA OR onnxruntime_USE_NV OR onnxruntime_USE_TENSORRT)
  message(FATAL_ERROR "standalone_winml_perf_test must not be combined with CUDA/NV/TensorRT — the WinML runtime owns provider loading")
endif()

# Windows SDK platform-version guard. The flat-C WinMLEpCatalog API is
# available starting in 10.0.26100. Mirrors the PerceptiveShell PR #38965
# ortloader_winml.cmake guard.
set(REQUIRED_PLATFORM_VERSION "10.0.26100.0")
if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION VERSION_LESS REQUIRED_PLATFORM_VERSION)
  message(FATAL_ERROR
    "standalone_winml_perf_test requires the Windows SDK target platform version >= ${REQUIRED_PLATFORM_VERSION}, "
    "but CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION='${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}'.")
endif()

message(STATUS "Using WINML_PACKAGE_VERSION: ${WINML_PACKAGE_VERSION}")

#-------------------------------------------------------------------------------
# Fetch and resolve Microsoft.Windows.AI.MachineLearning

include(FetchContent)

FetchContent_Declare(
  NuGetCMakePackage
  GIT_REPOSITORY https://github.com/mschofie/NuGetCMakePackage
  GIT_TAG dc9e92672c6eb1c11f0d29d4f94731b3404cc096
)

FetchContent_MakeAvailable(NuGetCMakePackage)

add_nuget_packages(
  PACKAGES
  Microsoft.Windows.AI.MachineLearning ${WINML_PACKAGE_VERSION}
)

# NOTE: The exact filename / casing exposed by add_nuget_packages may differ
# from the PerceptiveShell PR's nugetDL flow. If find_package fails, inspect
# build/__nuget/microsoft.windows.ai.machinelearning.${WINML_PACKAGE_VERSION}/
# (or wherever NuGetCMakePackage drops it) for the *Config.cmake filename and
# adjust this call accordingly.
find_package(Microsoft.Windows.AI.MachineLearning CONFIG REQUIRED)

#-------------------------------------------------------------------------------
# Source files
set(standalone_winml_perf_test_src_dir ${TEST_SRC_DIR}/perftest)

set(standalone_winml_perf_test_src_patterns
  "${standalone_winml_perf_test_src_dir}/*.cc"
  "${standalone_winml_perf_test_src_dir}/*.h"
  "${standalone_winml_perf_test_src_dir}/windows/*.cc"
  "${standalone_winml_perf_test_src_dir}/windows/*.h")

file(GLOB standalone_winml_perf_test_src CONFIGURE_DEPENDS
  ${standalone_winml_perf_test_src_patterns}
)

#-------------------------------------------------------------------------------
# Executable
onnxruntime_add_executable(standalone_winml_perf_test
  ${standalone_winml_perf_test_src}
  ${standalone_winml_perf_test_src_dir}/windows/app.manifest
  ${ONNXRUNTIME_ROOT}/core/platform/path_lib.cc
)

target_compile_options(standalone_winml_perf_test PRIVATE ${disabled_warnings})

target_include_directories(standalone_winml_perf_test PRIVATE
  ${onnx_test_runner_src_dir}
  ${ONNXRUNTIME_ROOT}
  ${onnxruntime_graph_header}
  ${onnxruntime_exec_src_dir}
  ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(standalone_winml_perf_test
  PRIVATE
  ORT_API_MANUAL_INIT
  BUILD_STANDALONE_WINML_PERF_TEST
  ABSL_FLAGS_STRIP_NAMES=0
)

# ORT_API_MANUAL_INIT must be consistent across all linked objects (enforced by
# #pragma detect_mismatch in onnxruntime_cxx_api.h:159-180). It is propagated
# to onnxruntime_perf_test, onnx_test_runner_common, and onnxruntime_test_utils
# in cmake/onnxruntime_unittests.cmake (gated by the same option).

target_link_libraries(standalone_winml_perf_test
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

  # Link only the WinML API surface — never WindowsML::OnnxRuntime, which
  # would conflict (LNK2005) with the locally-built onnxruntime.dll loaded
  # at runtime via LoadLibraryExW.
  WindowsML::Api

  onecoreuap.lib  # appmodel APIs
)

# Build the local onnxruntime.dll (and providers_shared, if separate) before
# this exe so POST_BUILD copy can stage them sibling to the exe.
add_dependencies(standalone_winml_perf_test onnxruntime onnxruntime_providers_shared)

#-------------------------------------------------------------------------------
# POST_BUILD: stage runtime DLLs sibling to the exe so LoadLibraryExW + the
# WinML loader can find them without PATH gymnastics.
#
# - onnxruntime.dll: locally built (NOT the copy inside the WinML NuGet).
# - onnxruntime_providers_shared.dll: copied defensively. dumpbin /dependents
#   on a registered EP DLL should determine whether it is actually required;
#   if not, drop this copy.
# - WindowsML::Api IMPORTED_LOCATION: the WinML API DLL itself.
# - DirectML.dll: required by WinML EPs at runtime. Path resolved via the
#   IMPORTED target if available, else hardcoded relative to
#   ${MICROSOFT.WINDOWS.AI.MACHINELEARNING_DIR}.

add_custom_command(TARGET standalone_winml_perf_test POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:onnxruntime>"
    "$<TARGET_FILE_DIR:standalone_winml_perf_test>"
  COMMENT "Staging local onnxruntime.dll next to standalone_winml_perf_test.exe"
)

add_custom_command(TARGET standalone_winml_perf_test POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:onnxruntime_providers_shared>"
    "$<TARGET_FILE_DIR:standalone_winml_perf_test>"
  COMMENT "Staging local onnxruntime_providers_shared.dll next to standalone_winml_perf_test.exe (defensive; verify need with dumpbin)"
)

# WindowsML::Api -> the API DLL. Use IMPORTED_LOCATION so we are robust to
# package-layout casing differences.
get_target_property(_winml_api_dll WindowsML::Api IMPORTED_LOCATION)
if(NOT _winml_api_dll)
  get_target_property(_winml_api_dll WindowsML::Api IMPORTED_LOCATION_RELEASE)
endif()
if(_winml_api_dll)
  add_custom_command(TARGET standalone_winml_perf_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_winml_api_dll}"
      "$<TARGET_FILE_DIR:standalone_winml_perf_test>"
    COMMENT "Staging WindowsML::Api DLL next to standalone_winml_perf_test.exe"
  )
else()
  message(WARNING "WindowsML::Api IMPORTED_LOCATION is not set — Microsoft.Windows.AI.MachineLearning.dll will not be staged automatically. Verify the package layout.")
endif()

# DirectML.dll location: the WinML config exposes WINML_DIRECTML_DLL directly
# (DirectML target is INTERFACE so it has no IMPORTED_LOCATION). Fall back to
# WindowsML::DirectML interface or to a hand-computed path under WINML_BINARY_DIR.
set(_directml_dll "")
if(DEFINED WINML_DIRECTML_DLL AND EXISTS "${WINML_DIRECTML_DLL}")
  set(_directml_dll "${WINML_DIRECTML_DLL}")
endif()
if(NOT _directml_dll AND TARGET WindowsML::DirectML)
  get_target_property(_directml_dll WindowsML::DirectML IMPORTED_LOCATION)
endif()
if(NOT _directml_dll AND DEFINED WINML_BINARY_DIR)
  set(_candidate "${WINML_BINARY_DIR}/DirectML.dll")
  if(EXISTS "${_candidate}")
    set(_directml_dll "${_candidate}")
  endif()
endif()
if(NOT _directml_dll)
  if(CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64")
    set(_winml_arch "arm64")
  elseif(CMAKE_VS_PLATFORM_NAME STREQUAL "x64" OR CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_winml_arch "x64")
  else()
    set(_winml_arch "x86")
  endif()
  # Final fallback: NuGet package layout under the standard __nuget directory.
  set(_candidate "${CMAKE_BINARY_DIR}/__nuget/Microsoft.Windows.AI.MachineLearning.${WINML_PACKAGE_VERSION}/runtimes/win-${_winml_arch}/native/DirectML.dll")
  if(EXISTS "${_candidate}")
    set(_directml_dll "${_candidate}")
  endif()
endif()
if(_directml_dll)
  add_custom_command(TARGET standalone_winml_perf_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_directml_dll}"
      "$<TARGET_FILE_DIR:standalone_winml_perf_test>"
    COMMENT "Staging DirectML.dll next to standalone_winml_perf_test.exe"
  )
else()
  message(WARNING "DirectML.dll not located via WINML_DIRECTML_DLL, WindowsML::DirectML, or package layout — staging skipped. Inspect the WinML NuGet directory and adjust cmake/standalone_winml_perf_test.cmake.")
endif()

# Note: the WinML NuGet ships its own onnxruntime.dll under runtimes/.../native.
# We deliberately do NOT copy it — the locally built ORT (already staged above)
# is the one we want loaded.
