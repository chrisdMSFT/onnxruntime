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

int RunPerfTest(Ort::Env& env, const perftest::PerformanceTestConfig& test_config);
Ort::Status CompileEpContextModel(Ort::Env& env, const perftest::PerformanceTestConfig& test_config);

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

  int status = 0;

  // EP context perf test
  if (test_config.run_config.compile_ep_context) {
    {
      std::cout << "\n> Compiling model...\n";
      auto compile_status = CompileEpContextModel(env, test_config);

      if (!compile_status.IsOK())
        return -1;
    }

    std::cout << "Model compiled successfully to " << ToUTF8String(test_config.run_config.compile_model_path) << "\n";
    if (test_config.run_config.compile_only) {
      return 0;
    }

    std::cout << "\n> Running EP context model...\n";
    {
      test_config.model_info.model_file_path = test_config.run_config.compile_model_path;
      status = RunPerfTest(env, test_config);
    }
  } else {
    // regular perf test
    status = RunPerfTest(env, test_config);
  }
  return status;
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

int RunPerfTest(Ort::Env& env, const perftest::PerformanceTestConfig& test_config) {
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

Ort::Status CompileEpContextModel(Ort::Env& env, const perftest::PerformanceTestConfig& test_config) {
  auto output_ctx_model_path = test_config.run_config.compile_model_path;
  const auto provider_name = test_config.machine_config.provider_type_name;

  Ort::SessionOptions session_options;

  // Add EP devices if any (created by plugin EP)
  if (!test_config.registered_plugin_eps.empty()) {
    perftest::utils::AppendPluginExecutionProviders(env, session_options, test_config);
  } else {
    // Regular non-plugin EP
    std::unordered_map<std::string, std::string> provider_options;
    session_options.AppendExecutionProvider(provider_name, provider_options);
  }

  // free dim override
  if (!test_config.run_config.free_dim_name_overrides.empty()) {
    for (auto const& dim_override : test_config.run_config.free_dim_name_overrides) {
      if (g_ort->AddFreeDimensionOverrideByName(session_options, ToUTF8String(dim_override.first).c_str(), dim_override.second) != nullptr) {
        fprintf(stderr, "AddFreeDimensionOverrideByName failed for named dimension: %s\n", ToUTF8String(dim_override.first).c_str());
      } else {
        fprintf(stdout, "Overriding dimension with name, %s, to %d\n", ToUTF8String(dim_override.first).c_str(), (int)dim_override.second);
      }
    }
  }

  if (!test_config.run_config.free_dim_denotation_overrides.empty()) {
    for (auto const& dim_override : test_config.run_config.free_dim_denotation_overrides) {
      if (g_ort->AddFreeDimensionOverride(session_options, ToUTF8String(dim_override.first).c_str(), dim_override.second) != nullptr) {
        fprintf(stderr, "AddFreeDimensionOverride failed for dimension denotation: %s\n", ToUTF8String(dim_override.first).c_str());
      } else {
        fprintf(stdout, "Overriding dimension with denotation, %s, to %d\n", ToUTF8String(dim_override.first).c_str(), (int)dim_override.second);
      }
    }
  }

  Ort::ModelCompilationOptions model_compile_options(env, session_options);
  model_compile_options.SetEpContextEmbedMode(test_config.run_config.compile_binary_embed);
  model_compile_options.SetInputModelPath(test_config.model_info.model_file_path.c_str());
  model_compile_options.SetOutputModelPath(output_ctx_model_path.c_str());

  Ort::Status status;
  std::chrono::duration<double> compile_duration;
  {
    auto compile_time_start = std::chrono::high_resolution_clock::now();
    status = Ort::CompileModel(env, model_compile_options);
    auto compile_time_end = std::chrono::high_resolution_clock::now();
    compile_duration = compile_time_end - compile_time_start;
  }

  if (!status.IsOK()) {
    std::cout << "Failed to compile model: " << status.GetErrorMessage() << std::endl;
  } else {
    std::cout << "Compile time cost: " << compile_duration.count() << " s\n";
  }
  return status;
}
