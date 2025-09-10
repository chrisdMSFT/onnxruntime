// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <core/session/onnxruntime_c_api.h>
#include <random>
#include "command_args_parser.h"
#include "performance_runner.h"
#include "utils.h"
#include "strings_helper.h"
#include <google/protobuf/stubs/common.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.Windows.AI.MachineLearning.h>

// #include <MddBootstrap.h>

// #include <WindowsAppSDK-VersionInfo.h>
// #include <MddBootstrap.h>

// namespace MddBootstrap { using namespace ::Microsoft::Windows::ApplicationModel::DynamicDependency::Bootstrap; }

using namespace winrt::Windows::Foundation::Collections;

using namespace onnxruntime;
const OrtApi* g_ort = NULL;

#ifdef _WIN32
int real_main(int argc, wchar_t* argv[]) {
#else
int real_main(int argc, char* argv[]) {
#endif

  winrt::init_apartment();

  g_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
  std::wcout << "Ort::GetApi() 0x" << &Ort::GetApi() << std::endl;

  Ort::Env env;

  std::wcout << "1. GetEpDevices" << std::endl;
  auto devices = env.GetEpDevices();
  for (const auto& device : devices) {
    std::cout << device.EpName() << " " << std::endl;
  }

  perftest::PerformanceTestConfig test_config;

  std::cout << std::endl
            << "Getting available providers..." << std::endl
            << std::endl;
  auto catalog = winrt::Microsoft::Windows::AI::MachineLearning::ExecutionProviderCatalog::GetDefault();
  auto providers = catalog.FindAllProviders();

  for (const auto& provider : providers) {
    std::wcout << "Provider: " << provider.Name().c_str();
   try {
      auto readyState = provider.ReadyState();
      std::wcout << " Ready state: " << static_cast<int>(readyState);

      if (readyState != winrt::Microsoft::Windows::AI::MachineLearning::ExecutionProviderReadyState::Ready) {
        std::wcout << " --> EnsureReadyAsync";
        provider.EnsureReadyAsync().get();
      }

      auto registerResult = provider.TryRegister();
      std::wcout << "  --> TryRegister(" << registerResult << ") ";

      std::wcout << " --> FIN!";
    } catch (...) {
      std::wcout << "  [ERROR!!!]";
    }

    std::wcout << std::endl;
  }

  if (!perftest::CommandLineParser::ParseArguments(test_config, argc, argv)) {
    fprintf(stderr, "%s", "See 'onnxruntime_perf_test --help'.");
    return -1;
  }

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

  std::wcout << "test_config.use_winml_ep: " << test_config.use_winml_ep << std::endl;

  // std::wcout << std::endl;

  // std::wcout << "2. GetEpDevices" << std::endl;

  // devices = env.GetEpDevices();
  // for (const auto& device : devices) {
  //   std::cout << device.EpName() << ":" << std::endl;
  //   for (const auto& kv : device.EpMetadata().GetKeyValuePairs()) {
  //     std::cout << " " << kv.first << "=" << kv.second << std::endl;
  //   }

  //   std::cout << std::endl;
  // }

  if (!test_config.plugin_ep_names_and_libs.empty()) {
    std::wcout << "RegisterExecutionProviderLibrary" << std::endl;
    perftest::utils::RegisterExecutionProviderLibrary(env, test_config);
  }

  // test_config.registered_plugin_eps.push_back(provider.Name());


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

  ::google::protobuf::ShutdownProtobufLibrary();

  return retval;
}
