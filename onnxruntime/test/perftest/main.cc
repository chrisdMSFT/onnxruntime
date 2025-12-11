// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <filesystem>
#include <iosfwd>
#include <Unknwn.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Microsoft.Windows.AI.MachineLearning.h>
#include <string>
#include <vector>
#include <core/session/onnxruntime_c_api.h>
#include <random>

#include "command_args_parser.h"
#include "performance_runner.h"
#include "utils.h"
#include "strings_helper.h"
#include <google/protobuf/stubs/common.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.Windows.AI.MachineLearning.h>

#include <appmodel.h>
#include <WindowsAppSDK-VersionInfo.h>

static wchar_t* g_packageFullName = nullptr;
static wchar_t* g_packageDependencyId = nullptr;
static HRESULT g_initializationResult = E_NOT_VALID_STATE;
static PACKAGEDEPENDENCY_CONTEXT g_packageContext = nullptr;

static inline bool IsRunningOnArm64()
{
#if defined(_M_ARM64EC) || defined(_M_ARM64)
    return true;
#else
    static const bool isArm64Native = [] {
        USHORT processMachine{};
        USHORT nativeMachine{};
        const auto result{::IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine)};
        return (0 == result) || (nativeMachine == IMAGE_FILE_MACHINE_ARM64);
    }();
    return isArm64Native;
#endif
}

static inline PackageDependencyProcessorArchitectures GetPackageDependencyProcessorArchitectures()
{
#if defined(_M_ARM64)
    const PackageDependencyProcessorArchitectures architectures = PackageDependencyProcessorArchitectures_Arm64;
#elif defined(_M_X64)
    const PackageDependencyProcessorArchitectures architectures = PackageDependencyProcessorArchitectures_X64;
#elif defined(_M_ARM64EC)
    const PackageDependencyProcessorArchitectures architectures =
        IsRunningOnArm64() ? PackageDependencyProcessorArchitectures_Arm64 : PackageDependencyProcessorArchitectures_X64;
#endif
    return architectures;
}

HRESULT ME_WinMLInitialize(const wchar_t* const packageFamilyName)
{
    if (g_packageDependencyId != nullptr)
    {
        return g_initializationResult;
    }

    // Create the package dependency
    {
        PSID userContext = nullptr;

        PACKAGE_VERSION minVersion {};

        const PackageDependencyProcessorArchitectures architectures = GetPackageDependencyProcessorArchitectures();

        const PackageDependencyLifetimeKind lifetimeKind = PackageDependencyLifetimeKind_Process;

        const wchar_t* lifetimeArtifact = nullptr;

        CreatePackageDependencyOptions options = CreatePackageDependencyOptions_None;

        HRESULT result = S_OK;
        result = TryCreatePackageDependency(
            userContext,
            packageFamilyName,
            minVersion,
            architectures,
            lifetimeKind,
            lifetimeArtifact,
            options,
            &g_packageDependencyId);

        if (FAILED(result))
        {
            return result;
        }

        if (!g_packageDependencyId)
        {
            return E_UNEXPECTED;
        }
    }

    // Add the package dependency
    {
        int rank = 0;
        AddPackageDependencyOptions options = AddPackageDependencyOptions_PrependIfRankCollision;

        g_initializationResult = AddPackageDependency(g_packageDependencyId, rank, options, &g_packageContext, &g_packageFullName);
    }

    return g_initializationResult;
}

void ME_WinMLUninitialize()
{
    if (g_packageDependencyId)
    {
        ::HeapFree(::GetProcessHeap(), 0, g_packageDependencyId);
        g_packageDependencyId = nullptr;
    }

    if (g_packageFullName)
    {
        ::HeapFree(::GetProcessHeap(), 0, g_packageFullName);
        g_packageFullName = nullptr;
    }

    if (g_packageContext)
    {
        ::RemovePackageDependency(g_packageContext);
        g_packageContext = nullptr;
    }

    g_initializationResult = E_NOT_VALID_STATE;
}

using namespace winrt::Windows::Foundation::Collections;

using namespace onnxruntime;
const OrtApi* g_ort = NULL;

int RunPerfTest(Ort::Env& env, const perftest::PerformanceTestConfig& test_config);
Ort::Status CompileEpContextModel(const Ort::Env& env, const perftest::PerformanceTestConfig& test_config);

const char* ensure_ready_result_string[] = {
    "InProgress",
    "Success",
    "Failure"};

const char* execution_provider_ready_state_string[] = {
    "Ready",
    "NotReady",
    "NotPresent"};

#ifdef _WIN32
int real_main(int argc, wchar_t* argv[]) {
#else
int real_main(int argc, char* argv[]) {
#endif

  std::wcout << "WINDOWSAPPSDK_RUNTIME_PACKAGE_FRAMEWORK_PACKAGEFAMILYNAME_W:" << WINDOWSAPPSDK_RUNTIME_PACKAGE_FRAMEWORK_PACKAGEFAMILYNAME_W << std::endl;
  std::wcout << "WINDOWSAPPSDK_RELEASE_MAJOR:" << WINDOWSAPPSDK_RELEASE_MAJOR << std::endl;
  std::wcout << "WINDOWSAPPSDK_RELEASE_MINOR:" << WINDOWSAPPSDK_RELEASE_MINOR << std::endl;
  std::wcout << "WINDOWSAPPSDK_RELEASE_PATCH:" << WINDOWSAPPSDK_RELEASE_PATCH << std::endl;

  HRESULT hr = ME_WinMLInitialize(WINDOWSAPPSDK_RUNTIME_PACKAGE_FRAMEWORK_PACKAGEFAMILYNAME_W);
  if (FAILED(hr))
  {
      std::cerr << "Failed to initialize WinML bootstrap: " << std::hex << hr << std::endl;
      return -1;
  }

  // std::wcout << "catalog" << std::endl;
  auto catalog = winrt::Microsoft::Windows::AI::MachineLearning::ExecutionProviderCatalog::GetDefault();

  // std::wcout << "FindAllProviders/EnsureReadyAsync" << std::endl;
  auto providers = catalog.FindAllProviders();
  for (const auto& provider : providers) {
    std::wcout << "[WinML] Provider: " << provider.Name().c_str();
    auto readyState = provider.ReadyState();
    std::wcout << "[WinML]  Ready state: " << execution_provider_ready_state_string[static_cast<int>(readyState)] << std::endl;

    auto ensure_ready_result = provider.EnsureReadyAsync().get();
    std::wcout << "[WinML]  ensure_ready_result:" << ensure_ready_result_string[static_cast<int>(ensure_ready_result.Status())];
    std::wcout << " DiagnosticText:" << ensure_ready_result.DiagnosticText().c_str() << std::endl;

    auto registration_result = provider.TryRegister();
    std::wcout << "[WinML]  registration_result:" << (registration_result ? "true" : "false") << std::endl;
  }

  g_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
  Ort::InitApi(g_ort);

  perftest::PerformanceTestConfig test_config;
  if (!perftest::CommandLineParser::ParseArguments(test_config, argc, argv)) {
    fprintf(stderr, "%s", "See 'onnxruntime_perf_test --help'.");

    std::wcout << std::endl;
    for (int i = 0; i < argc; ++i) {
      std::wcerr << "[" << i << "][" << argv[i] << "]" << std::endl;
    }

    std::wcout << std::endl;
    return -1;
  }

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

  auto devices = env.GetEpDevices();

  //   typedef enum OrtHardwareDeviceType {
  //   OrtHardwareDeviceType_CPU,
  //   OrtHardwareDeviceType_GPU,
  //   OrtHardwareDeviceType_NPU
  // } OrtHardwareDeviceType;

  std::cout << "-------------------------------------------" << std::endl;
  std::cout << "provider_Type_Name:" << test_config.machine_config.provider_type_name << std::endl;
  std::cout << "has_Required_Device_Type:" << test_config.has_required_device_type << std::endl;
  std::cout << "required_Device_Type:" << test_config.required_device_type << std::endl;
  std::cout << "-------------------------------------------" << std::endl;

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
    if (test_config.registered_plugin_eps.empty()) {
      fprintf(stdout, "No plugin execution provider libraries are registered. Please specify them using \"--plugin_ep_libs\"; otherwise, only CPU may be available.\n");
    }
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

  ::google::protobuf::ShutdownProtobufLibrary();

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

Ort::Status CompileEpContextModel(const Ort::Env& env, const perftest::PerformanceTestConfig& test_config) {
  auto output_ctx_model_path = test_config.run_config.compile_model_path;
  const auto provider_name = test_config.machine_config.provider_type_name;

  Ort::SessionOptions session_options;

  std::unordered_map<std::string, std::string> provider_options;
  session_options.AppendExecutionProvider(provider_name, provider_options);

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
