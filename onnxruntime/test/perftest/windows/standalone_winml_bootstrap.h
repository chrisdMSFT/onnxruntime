// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
//
// Bootstrap for the standalone_winml_perf_test target. Provides:
//
//  1. An OrtGetApiBase() shim that loads onnxruntime.dll from the directory
//     containing the host exe (LOAD_WITH_ALTERED_SEARCH_PATH) and forwards
//     to the loaded module's OrtGetApiBase symbol. This satisfies the
//     unresolved-symbol reference in main.cc:27 without linking against
//     onnxruntime.lib (the locally built ORT is loaded at runtime).
//
//  2. StandaloneWinML_RegisterAllProviders / Unregister*: enumerates the
//     standalone Microsoft.Windows.AI.MachineLearning runtime's execution
//     provider catalog via the flat-C WinMLEpCatalog* API, ensures each EP
//     is ready, and registers it into the supplied OrtEnv via
//     OrtApi::RegisterExecutionProviderLibrary. Each registered EP name is
//     also pushed into PerformanceTestConfig::registered_plugin_eps so the
//     CompileEpContextModel() and OnnxRuntimeTestSession plugin V2 paths
//     pick them up.
//
// Cleanup is symmetric and *must* run while the OrtEnv is still alive —
// see the gsl::finally in main.cc.

#pragma once

#include <core/session/onnxruntime_c_api.h>

#include "test/perftest/test_configuration.h"

// Match the public declaration in onnxruntime_c_api.h exactly: ORT_EXPORT
// const OrtApiBase* ORT_API_CALL OrtGetApiBase(void) NO_EXCEPTION;
// On Windows ORT_API_CALL is __stdcall — getting the calling convention
// wrong here would corrupt the stack at link/load time.
extern "C" const OrtApiBase* ORT_API_CALL OrtGetApiBase(void) NO_EXCEPTION;

namespace onnxruntime {
namespace perftest {

// Enumerates the WinML EP catalog and registers each EP into `env` via
// OrtApi::RegisterExecutionProviderLibrary. Pushes each registered EP name
// into test_config.registered_plugin_eps. Safe to call once per process.
void StandaloneWinML_RegisterAllProviders(OrtEnv* env,
                                          const OrtApi* ort_api,
                                          PerformanceTestConfig& test_config);

// Symmetric cleanup. Calls OrtApi::UnregisterExecutionProviderLibrary for
// each previously-registered EP, then releases the catalog handle. Also
// removes the WinML-registered EP names from
// test_config.registered_plugin_eps and
// test_config.machine_config.plugin_provider_type_list so the existing
// perftest cleanup paths in main.cc / common_utils.cc do not try to
// double-unregister them. Must be called while `env` is still alive
// (registration is env-scoped). Never throws.
void StandaloneWinML_UnregisterAllProviders(OrtEnv* env,
                                            const OrtApi* ort_api,
                                            PerformanceTestConfig& test_config) noexcept;

}  // namespace perftest
}  // namespace onnxruntime
