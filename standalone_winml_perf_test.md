# standalone_winml_perf_test

A Windows-only build flavor of the ONNX Runtime perf test that loads ORT
through the **standalone** `Microsoft.Windows.AI.MachineLearning` runtime
via the flat-C `WinMLEpCatalog*` API. Unlike the WindowsAppSDK perf test
variant, this target:

- Does **not** depend on the Windows App SDK.
- Does **not** use C++/WinRT projection.
- Loads a **locally-built** `onnxruntime.dll` deployed sibling to the exe
  via a hand-rolled `OrtGetApiBase` shim (`LoadLibraryExW` +
  `GetProcAddress`) — so you can iterate on ORT changes without rebuilding
  the WinML runtime.
- Discovers and registers EPs from the WinML EP catalog using the flat-C
  `WinMLEpCatalogCreate` / `WinMLEpCatalogEnumProviders` /
  `WinMLEpEnsureReady` / `OrtApi::RegisterExecutionProviderLibrary` API.

## Build

Required:

- Windows 11 SDK with target platform version `10.0.26100.0` or later.
- Visual Studio 2022 (MSVC).
- Internet access (for the one-time `Microsoft.Windows.AI.MachineLearning`
  NuGet restore).

Configure (x64):

```cmd
cmake -B build ^
  -A x64 -T host=x64 -G "Visual Studio 17 2022" ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -Donnxruntime_BUILD_STANDALONE_WINML_PERF_TEST=ON ^
  -DWINML_PACKAGE_VERSION=2.0.297-preview ^
  -Donnxruntime_BUILD_SHARED_LIB=ON ^
  -Donnxruntime_BUILD_UNIT_TESTS=ON
```

Build:

```cmd
cmake --build build --config RelWithDebInfo --target standalone_winml_perf_test
```

The target:

- Adds the `Microsoft.Windows.AI.MachineLearning` NuGet via
  `add_nuget_packages`.
- `find_package(Microsoft.Windows.AI.MachineLearning CONFIG REQUIRED)` to
  locate the WinML SDK headers and import targets.
- Links **only** `WindowsML::Api` — never `WindowsML::OnnxRuntime`, which
  would `LNK2005` against the locally-built ORT.
- POST_BUILD copies these DLLs sibling to the exe so the runtime resolves
  them via `LOAD_WITH_ALTERED_SEARCH_PATH`:
  - `onnxruntime.dll` (locally built — `$<TARGET_FILE:onnxruntime>`).
  - `onnxruntime_providers_shared.dll` (locally built; defensive, may be
    droppable after `dumpbin /dependents` confirms WinML EPs do not need
    it).
  - `Microsoft.Windows.AI.MachineLearning.dll` (the WinML API DLL,
    resolved from `WindowsML::Api`'s `IMPORTED_LOCATION`).
  - `DirectML.dll` (resolved from `WindowsML::DirectML` if exposed,
    else from the WinML NuGet's `runtimes/win-${arch}/native/` directory).

`ORT_API_MANUAL_INIT` is defined for every TU that links into this target
(`onnxruntime_perf_test`, `onnx_test_runner_common`, `onnxruntime_test_utils`)
so the C++ wrapper code uses the manually-initialized `Ort::g_api` pointer
rather than calling `OrtGetApiBase()` lazily.

## Run

List all EP devices the standalone WinML runtime knows about:

```cmd
build\RelWithDebInfo\standalone_winml_perf_test.exe --list_ep_devices
```

Run inference filtered by device type:

```cmd
build\RelWithDebInfo\standalone_winml_perf_test.exe ^
  -e <ProviderName> --required_device_type <cpu|gpu|npu> ^
  -t 10 -I path\to\model.onnx
```

Compile to an EP-context model (cache) and run from the cache:

```cmd
:: Step 1: compile
build\RelWithDebInfo\standalone_winml_perf_test.exe ^
  -e <ProviderName> --required_device_type <cpu|gpu|npu> -r 1 ^
  -C "ep.context_enable|1 ep.context_file_path|out\model.cache.onnx" ^
  -I path\to\model.onnx

:: Step 2: run from cache
build\RelWithDebInfo\standalone_winml_perf_test.exe ^
  -e <ProviderName> --required_device_type <cpu|gpu|npu> -t 10 ^
  -I out\model.cache.onnx
```

### Flags retained from the WindowsAppSDK perf-test variant

- `--required_device_type {cpu|gpu|npu}` — filter EP devices by hardware
  type after the WinML runtime's catalog enumeration.

### Flags **not** present in this target

- `--winappsdk_version` — the standalone runtime is not pinned to a WASDK
  major.minor.
- `--winappsdk_register_provider` — registration is governed by what the
  catalog enumerates plus the `--required_device_type` filter.

## Architecture

```
+--------------------------------------------------------------+
|  standalone_winml_perf_test.exe (this target)                |
|                                                              |
|   main.cc                                                    |
|     |                                                        |
|     | calls OrtGetApiBase()                                  |
|     v                                                        |
|   standalone_winml_bootstrap.cc                              |
|     OrtGetApiBase shim:                                      |
|       LoadLibraryExW(<exe_dir>\onnxruntime.dll,              |
|                      LOAD_WITH_ALTERED_SEARCH_PATH)          |
|       GetProcAddress("OrtGetApiBase") -> forward             |
|                                                              |
|     StandaloneWinML_RegisterAllProviders(env, g_ort, cfg):   |
|       WinMLEpCatalogCreate                                   |
|       WinMLEpCatalogEnumProviders                            |
|         per EP:                                              |
|           WinMLEpEnsureReady                                 |
|           WinMLEpGetLibraryPath                              |
|           OrtApi::RegisterExecutionProviderLibrary           |
|           push name into cfg.registered_plugin_eps           |
|                              cfg.machine_config              |
|                                 .plugin_provider_type_list   |
|                                                              |
|     StandaloneWinML_UnregisterAllProviders (gsl::finally):   |
|       OrtApi::UnregisterExecutionProviderLibrary (each EP)   |
|       remove WinML EP names from cfg vectors                 |
|       WinMLEpCatalogRelease                                  |
+--------------------------------------------------------------+
                           |
                           | LoadLibraryExW
                           v
+--------------------------------------------------------------+
|  onnxruntime.dll (locally built, staged sibling to exe)      |
|     OrtGetApiBase, OrtApi::*, EP registration plumbing       |
+--------------------------------------------------------------+
                           |
                           | RegisterExecutionProviderLibrary
                           v
+--------------------------------------------------------------+
|  EP DLLs registered with ORT                                 |
|     enumerated from the standalone WinML EP catalog,         |
|     loaded from their MSIX-installed locations               |
+--------------------------------------------------------------+
```

## Critical correctness invariants

1. **`OrtGetApiBase` shim retained** — `main.cc:31` calls
   `OrtGetApiBase()->GetApi(ORT_API_VERSION)`. Our shim forwards to the
   locally-staged `onnxruntime.dll`. On failure the shim aborts the
   process with a diagnostic.
2. **EPs register into the main `OrtEnv`** — bootstrap accepts `env` as a
   parameter; `ort_test_session.cc`'s `env.GetEpDevices()` reads from the
   same env.
3. **Cleanup runs while `env` is alive** — symmetric Unregister wrapped
   in `gsl::finally` inside `real_main`. Declared *after* the existing
   plugin-EP cleanup so LIFO ordering runs ours first; our cleanup also
   strips its EP names from `test_config.registered_plugin_eps` so the
   existing cleanup does not double-unregister them.
4. **Plugin V2 selection populated** — bootstrap appends each registered
   EP name to `test_config.machine_config.plugin_provider_type_list`
   (only when the user has not supplied `--plugin_eps`), so
   `AppendPluginExecutionProviders` can match `env.GetEpDevices()` to the
   set of EPs and `CompileEpContextModel` takes the plugin V2 branch.
5. **Link `WindowsML::Api` only** — never `WindowsML::OnnxRuntime`.
6. **Locally-built `onnxruntime.dll` is the one deployed** —
   `$<TARGET_FILE:onnxruntime>`, not the WinML NuGet's copy.

## Known risks

- `Microsoft.Windows.AI.MachineLearning v2.0.297-preview` availability via
  `add_nuget_packages` (mschofie/NuGetCMakePackage). Use
  `-DWINML_PACKAGE_VERSION=<other>` to bump.
- `find_package(Microsoft.Windows.AI.MachineLearning CONFIG REQUIRED)`
  casing — the actual NuGet may expose the config under a
  differently-cased filename. Inspect
  `build\__nuget\microsoft.windows.ai.machinelearning.<ver>\` if configure
  fails.
- The locally-built `onnxruntime.dll` and the WinML EP MSIX stack must be
  ABI-compatible. If the MSIX EP refuses to bind, the WinML runtime owner
  must be consulted.
- `onnxruntime_providers_shared.dll` is staged defensively; verify with
  `dumpbin /dependents` on a registered EP DLL whether it is required.
