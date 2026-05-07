// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

// onnxruntime dependencies
#include <core/session/onnxruntime_c_api.h>
#include <random>
#include "command_args_parser.h"
#include "performance_runner.h"
#include "utils.h"
#include "strings_helper.h"
#include <google/protobuf/stubs/common.h>

#include <gsl/util>
#ifdef BUILD_WINML_STANDALONE_PERF_TEST
#include "windows/winml_standalone.h"
#endif

using namespace onnxruntime;
const OrtApi* g_ort = NULL;

#ifdef _WIN32
int real_main(int argc, wchar_t* argv[]) {
#else
int real_main(int argc, char* argv[]) {
#endif
  perftest::PerformanceTestConfig test_config;
  if (!perftest::CommandLineParser::ParseArguments(test_config, argc, argv)) {
    fprintf(stderr, "%s", "See 'onnxruntime_perf_test --help'.");
#ifdef BUILD_WINML_STANDALONE_PERF_TEST
    std::wcout << std::endl;
    for (int i = 0; i < argc; ++i) {
      std::wcerr << "[" << i << "][" << argv[i] << "]" << std::endl;
    }
    std::wcout << std::endl;
#endif
    return -1;
  }

#ifdef BUILD_WINML_STANDALONE_PERF_TEST
  // We require the runtime to support our compile-time API version (newer is
  // fine because GetApi(N) returns a v_N-shaped struct that the runtime is
  // forward-compatible with; older is NOT fine because the v25 headers
  // describe a struct layout the older runtime never allocated, so any
  // v25-only call would dereference past the actual struct -> UB).
  {
    const OrtApiBase* api_base = OrtGetApiBase();
    if (api_base == nullptr) {
      fprintf(stderr, "[WinML Standalone] Failed to get OrtApiBase (onnxruntime.dll not found or failed to load).\n");
      return -1;
    }
    g_ort = api_base->GetApi(ORT_API_VERSION);
    if (g_ort == nullptr) {
      fprintf(stderr,
              "[WinML Standalone] onnxruntime.dll does not support ORT API version %d. "
              "Please update the bundled WinML NuGet package or the runtime DLL beside the EXE.\n",
              ORT_API_VERSION);
      return -1;
    }
  }
#else
  g_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
#endif
  if (g_ort == nullptr) {
    fprintf(stderr, "Failed to get ONNX Runtime C API.\n");
    return -1;
  }
  Ort::InitApi(g_ort);

  // Setup the Onnxruntime environment
  Ort::Env env{nullptr};
  {
    bool failed = false;
    ORT_TRY {
      OrtLoggingLevel logging_level = test_config.run_config.f_verbose
                                          ? ORT_LOGGING_LEVEL_VERBOSE
                                          : ORT_LOGGING_LEVEL_WARNING;
      env = Ort::Env(logging_level, "Default");
    }
    ORT_CATCH(const Ort::Exception& e) {
      ORT_HANDLE_EXCEPTION([&]() {
        std::cerr << "Error creating environment: " << e.what() << std::endl;
        failed = true;
      });
    }

    if (failed)
      return -1;
  }

#ifdef BUILD_WINML_STANDALONE_PERF_TEST
  // RAII: registers EP providers via the WinML EP catalog now and
  // unregisters + releases the catalog at scope exit. Destruction must
  // happen before `env` is destroyed (the destructor calls
  // UnregisterExecutionProviderLibrary, which dives through the C API),
  // so this object is declared on the function stack after `env` is
  // constructed.
  WinMLStandaloneRegistration winml_session{env, test_config.winml_register_provider};
  std::cout << "ONNX Runtime C++ API version: " << ORT_API_VERSION << std::endl;

  std::cout << "-------------------------------------------" << std::endl;
  std::cout << "[WinML Standalone] provider_Type_Name:" << test_config.machine_config.provider_type_name << std::endl;
  std::cout << "[WinML Standalone] has_Required_Device_Type:" << test_config.has_required_device_type << std::endl;
  std::cout << "[WinML Standalone] required_Device_Type:" << test_config.required_device_type << std::endl;
  std::wcout << L"[WinML Standalone] model_file_path:" << test_config.model_info.model_file_path << std::endl;
  std::cout << "-------------------------------------------" << std::endl;
#endif

  if (!test_config.plugin_ep_names_and_libs.empty()) {
    perftest::utils::RegisterExecutionProviderLibrary(env, test_config);
  }

  // Unregister all registered plugin EP libraries before program exits.
  // This is necessary because unregistering the plugin EP also unregisters any associated shared allocators.
  // If we don't do this and program returns, the factories stored inside the environment will be destroyed when the environment goes out of scope.
  // Later, when the shared allocator's deleter runs, it may cause a segmentation fault because it attempts to use the already-destroyed factory to call ReleaseAllocator.
  // See "ep_device.ep_factory->ReleaseAllocator" in Environment::CreateSharedAllocatorImpl.
  auto unregister_plugin_eps_at_scope_exit = gsl::finally([&]() {
    if (!test_config.registered_plugin_eps.empty()) {
      perftest::utils::UnregisterExecutionProviderLibrary(env, test_config);  // this won't throw
    }
  });

  if (test_config.list_available_ep_devices) {
    perftest::utils::ListEpDevices(env);
#ifndef BUILD_WINML_STANDALONE_PERF_TEST
    if (test_config.registered_plugin_eps.empty()) {
      fprintf(stdout, "No plugin execution provider libraries are registered. Please specify them using \"--plugin_ep_libs\"; otherwise, only CPU may be available.\n");
    }
#endif
    return 0;
  }

  std::random_device rd;
  perftest::PerformanceRunner perf_runner(env, test_config, rd);

  // Exit if user enabled -n option so that user can measure session creation time
  if (test_config.run_config.exit_after_session_creation) {
    perf_runner.LogSessionCreationTime();
    return 0;
  }

  auto status = perf_runner.Run();
  if (!status.IsOK()) {
    printf("Run failed:%s\n", status.ErrorMessage().c_str());
    return -1;
  }

  perf_runner.SerializeResult();

  return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
  int retval = -1;

  ORT_TRY {
    retval = real_main(argc, argv);
  }

  ORT_CATCH(const std::exception& ex) {
    ORT_HANDLE_EXCEPTION([&]() {
      std::cerr << ex.what() << std::endl;
      retval = -1;
    });
  }

#ifdef BUILD_WINML_STANDALONE_PERF_TEST
  std::cout << "retval: " << retval << std::endl;
  std::cout << "Shutting down Protobuf library..." << std::endl;
#endif
  ::google::protobuf::ShutdownProtobufLibrary();

#ifdef BUILD_WINML_STANDALONE_PERF_TEST
  std::cout << "~fin~" << std::endl;
#endif

  return retval;
}
