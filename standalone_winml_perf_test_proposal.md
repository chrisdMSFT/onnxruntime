# Proposal — Convert `winappsdk_onnxruntime_perf_test` to Standalone WinML Mode

## Background

This branch (`G:\onnxruntime-chrisd`) ships a Windows-only perf-test variant
named **`winappsdk_onnxruntime_perf_test`** that loads ONNX Runtime through the
Windows App SDK (WASDK) ML runtime. PerceptiveShell PR
[#38965](https://devicesasg.visualstudio.com/PerceptiveShell/_git/PerceptiveShell/pullrequest/38965)
("Convert PerceptiveShell WinML test binaries to standalone mode") performs the
same architectural conversion for PerceptiveShell's WinML test binaries:

| Aspect | Old (WASDK pattern) | New (standalone WinML pattern) |
| --- | --- | --- |
| NuGet packages | 6: CppWinRT, ImplementationLibrary, WindowsAppSDK.{Runtime,Foundation,InteractiveExperiences,ML} | 1: `Microsoft.Windows.AI.MachineLearning` |
| EP discovery API | WinRT projection of `Microsoft.Windows.AI.MachineLearning.ExecutionProviderCatalog` (via cppwinrt-generated headers) | Flat-C `WinMLEpCatalog*` API from `<WinMLEpCatalog.h>` |
| Bootstrap | `TryCreatePackageDependency` + `AddPackageDependency` to load the WindowsAppRuntime framework package | None — `onnxruntime.dll` deployed next to the exe |
| `onnxruntime.dll` source | From inside the framework package | Locally-built sibling target |
| WinRT projection | Required (`#include <winrt/...>`) | Not used |
| EP registration | `provider.TryRegister()` on each `ExecutionProvider` | `OrtApi::RegisterExecutionProviderLibrary` per EP |

The goal of this proposal is to produce the same outcome here: replace the
WASDK-based bootstrap with a single `Microsoft.Windows.AI.MachineLearning`
NuGet, the flat-C `WinMLEpCatalog` API, and a locally deployed
`onnxruntime.dll`.

## User decisions confirmed

1. **Replace, don't coexist.** Drop the WASDK code path entirely.
2. **Rename target to `standalone_winml_perf_test`.** All identifiers
   (`onnxruntime_BUILD_*`, target name, exe, compile define) follow.
3. **`Microsoft.Windows.AI.MachineLearning` version is a CMake variable**,
   `WINML_PACKAGE_VERSION`, defaulting to `2.0.297-preview`.
4. **Drop `--winappsdk_version` and `--winappsdk_register_provider` flags**
   entirely (no backward-compat no-ops, no rename). `--required_device_type`
   is **kept** (consumed in `ort_test_session.cc` for EP-device filtering).

---

## Architectural shape after conversion

```
                         standalone_winml_perf_test.exe
                                      |
              +-----------------------+---------------------+
              |                                             |
              v                                             v
   ┌──────────────────────────────────┐   ┌──────────────────────────────────────┐
   │ standalone_winml_bootstrap.cc    │   │ main.cc (mostly unchanged)           │
   │                                  │   │                                      │
   │ extern "C" OrtGetApiBase():      │   │  g_ort = OrtGetApiBase()->GetApi(..) │
   │   static once-init →             │   │  Ort::InitApi(g_ort)                 │
   │   LoadLibraryExW(                │   │  env = Ort::Env(...)                 │
   │     L"onnxruntime.dll",          │   │  StandaloneWinML_RegisterAllProviders│
   │     LOAD_WITH_ALTERED_SEARCH_PATH│   │     (env, g_ort, &test_config)       │
   │   ) → GetProcAddress(            │   │  gsl::finally → unregister + release │
   │     "OrtGetApiBase")             │   │     while env still alive            │
   │                                  │   │  ... use env in perf runner ...      │
   │ Register(env, ortApi, cfg):      │   └──────────────────────────────────────┘
   │   WinMLEpCatalogCreate           │
   │   WinMLEpCatalogEnumProviders →  │   ┌──────────────────────────────────────┐
   │     EnsureReady, GetLibraryPath  │   │ ort_test_session.cc /                │
   │   ortApi->RegisterExecution      │   │ CompileEpContextModel                │
   │     ProviderLibrary(env, name,   │──→│  - env.GetEpDevices() finds          │
   │     libPath)                     │   │    catalog-registered EPs            │
   │   push name into                 │   │  - AppendExecutionProvider_V2 path   │
   │     test_config.registered_      │   │    works because                     │
   │     plugin_eps so compile path   │   │    registered_plugin_eps was         │
   │     also takes plugin V2 branch  │   │    populated by bootstrap            │
   │                                  │   └──────────────────────────────────────┘
   │ Unregister(env, ortApi):         │
   │   for each name:                 │
   │     UnregisterExecutionProvider  │
   │       Library(env, name)         │
   │   WinMLEpCatalogRelease          │
   └──────────────────────────────────┘
                  ▲
                  │ Loads at runtime
                  │
   ┌──────────────────────────────────┐    ┌──────────────────────────────┐
   │ onnxruntime.dll                  │    │ Microsoft.Windows.AI         │
   │ (locally built; copied next to   │    │   .MachineLearning.dll       │
   │ exe POST_BUILD via $<TARGET_FILE:│    │ DirectML.dll                 │
   │ onnxruntime>)                    │    │ onnxruntime_providers_       │
   │                                  │    │   shared.dll (locally built; │
   └──────────────────────────────────┘    │   copy iff still required)   │
                                           └──────────────────────────────┘
```

**Critical correctness points** (each was a review blocker we fixed):

- **`OrtGetApiBase` shim is retained.** `main.cc:50` calls `OrtGetApiBase()` to
  get the API pointer. The bootstrap defines a global `extern "C"
  OrtGetApiBase()` that loads the local `onnxruntime.dll` and forwards to its
  symbol — same pattern as today, just without the WASDK package-lookup
  scaffolding.
- **EPs register into the main env.** `OrtApi::RegisterExecutionProviderLibrary`
  is env-scoped. `ort_test_session.cc` later calls `env.GetEpDevices()` on the
  env created by `main.cc`. The bootstrap accepts that env as a parameter
  (`StandaloneWinML_RegisterAllProviders(env, ortApi, &test_config)`) instead of
  creating its own — otherwise `GetEpDevices` would return nothing.
- **Cleanup happens while the env is still alive.** Unregister + catalog
  release is wrapped in a `gsl::finally` inside `real_main`, mirroring the
  existing plugin EP cleanup at `main.cc:96-100`. The post-`real_main`
  shutdown hook in `wmain` is removed (env is already torn down by then).
- **Compile-EP-context path is wired.** The bootstrap pushes each registered
  EP name into `test_config.registered_plugin_eps` so
  `CompileEpContextModel()` (`main.cc:210-212`) takes the plugin V2 branch
  instead of the legacy `AppendExecutionProvider(provider_name)` fallback.

---

## Files affected

### Replace / heavily edit
- `cmake/winappsdk_onnxruntime_perf_test.cmake` → renamed
  `cmake/standalone_winml_perf_test.cmake`. Replaces the 6-package
  `add_nuget_packages` block with a single
  `Microsoft.Windows.AI.MachineLearning ${WINML_PACKAGE_VERSION}`. Adds the
  Windows SDK platform-version guard (mirrors PR's
  `ortloader_winml.cmake:6-11`):
  ```cmake
  set(REQUIRED_PLATFORM_VERSION "10.0.26100.0")
  if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION VERSION_LESS REQUIRED_PLATFORM_VERSION)
    message(FATAL_ERROR "...")
  endif()
  ```
  `find_package(Microsoft.Windows.AI.MachineLearning CONFIG REQUIRED)` — exact
  casing/dir is **a configure-time verification item**; the package, when
  restored via `add_nuget_packages`, may expose its config under a different
  filename / case than the PR's `nugetDL` flow. Link only `WindowsML::Api`
  (avoid `WindowsML::OnnxRuntime` to prevent `LNK2005` against the local ORT).
  POST_BUILD copy:
  - `$<TARGET_FILE:onnxruntime>` (locally built)
  - `$<TARGET_FILE:onnxruntime_providers_shared>` if the local ORT is built
    with that as a separate DLL (true today — see
    `cmake/onnxruntime_providers_cpu.cmake:222-229`)
  - `$<TARGET_PROPERTY:WindowsML::Api,IMPORTED_LOCATION>`
  - `${WINML_PACKAGE_DIR}/runtimes/win-${arch}/native/DirectML.dll`
  - `${WINML_PACKAGE_DIR}/runtimes/win-${arch}/native/onnxruntime.dll` is
    **deliberately not copied** — local build supplies it.
  Drop the `MICROSOFT_WINDOWSAPPSDK_ML_DISABLE_AUTOINITIALIZE` compile define
  (no WindowsAppSDK.ML reference). Rename `BUILD_WINAPPSDK_PERF_TEST` →
  `BUILD_STANDALONE_WINML_PERF_TEST` compile define.
  `add_dependencies(standalone_winml_perf_test onnxruntime onnxruntime_providers_shared)`.

- `cmake/CMakeLists.txt` — rename `onnxruntime_BUILD_WINAPPSDK_PERF_TEST` →
  `onnxruntime_BUILD_STANDALONE_WINML_PERF_TEST`; rename the listed cmake
  file include; remove the `CPPWINRT_VERSION` validation block (no longer
  used by anything in this target).

- `cmake/onnxruntime_unittests.cmake` — update the `EXCLUDE REGEX` so it
  filters the renamed bootstrap source (`standalone_winml_bootstrap` instead
  of `winappsdk_bootstrap`); update the comment about `ORT_API_MANUAL_INIT`
  propagation to reference the new target name.

- `onnxruntime/test/perftest/windows/winappsdk_bootstrap.{h,cc}` →
  rewritten as `standalone_winml_bootstrap.{h,cc}`. Public surface:
  ```cpp
  // Loads onnxruntime.dll next to the exe and forwards OrtGetApiBase.
  extern "C" const OrtApiBase* __cdecl OrtGetApiBase() noexcept;

  // Registers all WinML-discovered EPs into the supplied env via the
  // catalog API. Pushes registered EP names into
  // test_config.registered_plugin_eps so the CompileEpContextModel /
  // ort_test_session plugin V2 paths see them.
  void StandaloneWinML_RegisterAllProviders(
      OrtEnv* env,
      const OrtApi* ortApi,
      perftest::PerformanceTestConfig& test_config);

  // Symmetric cleanup. Must be called while `env` is still alive.
  void StandaloneWinML_UnregisterAllProviders(
      OrtEnv* env,
      const OrtApi* ortApi);
  ```
  Implementation mirrors the PR's `ortloader_winml.cpp` (in
  `files/pr38965/new/`):
  - `OrtGetApiBase()` is a once-initialized static that does
    `LoadLibraryExW(<exe_dir>/onnxruntime.dll, …, LOAD_WITH_ALTERED_SEARCH_PATH)`
    + `GetProcAddress("OrtGetApiBase")`. Module is intentionally never freed
    (matches current behavior at `winappsdk_bootstrap.cc:188-191`).
  - `Register*` calls `WinMLEpCatalogCreate`, `WinMLEpCatalogEnumProviders`,
    per-EP `WinMLEpGetReadyState` / `WinMLEpEnsureReady` /
    `WinMLEpGetLibraryPath`, then
    `ortApi->RegisterExecutionProviderLibrary(env, name, libraryPath)`.
  - File-scope state holds the catalog handle and the list of registered EP
    names so `Unregister*` can find them.
  - Optional MIGraphX blacklist (mirrors PR).

- `onnxruntime/test/perftest/main.cc` — rewire the call sites:
  - Rename `BUILD_WINAPPSDK_PERF_TEST` guards →
    `BUILD_STANDALONE_WINML_PERF_TEST`.
  - **Move** the registration call so it happens **after** `Ort::InitApi(g_ort)`
    and **after** `env = Ort::Env(...)`, not before. Pseudocode:
    ```cpp
    g_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    Ort::InitApi(g_ort);
    Ort::Env env(...);

    #ifdef BUILD_STANDALONE_WINML_PERF_TEST
      StandaloneWinML_RegisterAllProviders(env, g_ort, test_config);
      auto winml_cleanup = gsl::finally([&]() {
        StandaloneWinML_UnregisterAllProviders(env, g_ort);
      });
    #endif
    ```
  - Delete the `WinAppSDK_WinMLUninitialize()` call from `wmain` (cleanup is
    now scoped inside `real_main`).
  - Remove the `winrt::hresult_error` catch (no WinRT in standalone mode).
  - Update `[WinAppSDK]` log prefixes to `[WinML]` (cosmetic).

- `onnxruntime/test/perftest/command_args_parser.cc` — delete `ABSL_FLAG`
  declarations and parsing for `winappsdk_version` and
  `winappsdk_register_provider`. **Keep** `required_device_type` — it is
  consumed in `ort_test_session.cc:117-119`. Rename the
  `BUILD_WINAPPSDK_PERF_TEST` guards.

- `onnxruntime/test/perftest/test_configuration.h` — drop
  `winappsdk_version` and `winappsdk_register_provider` fields. Keep
  `has_required_device_type` and `required_device_type`. Rename guard.

- `onnxruntime/test/perftest/ort_test_session.cc` — rename
  `BUILD_WINAPPSDK_PERF_TEST` ifdefs and the `[WinAppSDK]` log prefixes
  (cosmetic). Behavior of `env.GetEpDevices()` + `AppendExecutionProvider_V2`
  path is unchanged because (a) registration now targets the same env this
  code reads from, and (b) `registered_plugin_eps` is now populated for the
  same reason.

### Keep as-is (review surfaced these)
- `onnxruntime/test/perftest/windows/app.manifest` — **retain** the
  `<supportedOS>` Windows 10 GUID block and `<maxversiontested>`. The
  earlier draft proposed dropping these; review concluded there is no
  justification and removal could change OS-version reporting to ORT and
  WinML APIs. Keep DPI awareness as well.

### Helper scripts (downstream of the rename)
- `chrisd/p-x64.cmd`, `chrisd/p-arm.cmd` — replace
  `-Donnxruntime_BUILD_WINAPPSDK_PERF_TEST=ON` with
  `-Donnxruntime_BUILD_STANDALONE_WINML_PERF_TEST=ON`; drop
  `-DCPPWINRT_VERSION=...`; optionally add
  `-DWINML_PACKAGE_VERSION=2.0.297-preview` for explicitness.
- `chrisd/b.cmd` — change `--target` value to `standalone_winml_perf_test`.
- `chrisd/go-*.cmd`, `chrisd/copy-perf-test.cmd`,
  `chrisd/simple-intel-test-ape.cmd`, `chrisd/go.cmd` — change the exe path
  and remove `--winappsdk_register_provider` / `--winappsdk_version` flag
  invocations (`go.cmd` and `go-all.cmd` are the affected ones).
- `chrisd/help.txt`, `chrisd/x64.txt`, `chrisd/arm.txt`,
  `chrisd/target_includes*.txt`, `chrisd/target-*.json` — captured output
  snapshots; either regenerate or leave with a "pre-conversion" note.

### Documentation
- `winappsdk_onnxruntime_perf_test.md` → rename to
  `standalone_winml_perf_test.md`. Rewrite Build section (single NuGet, no
  `CPPWINRT_VERSION`); drop the WinAppSDK runtime install / `Get-AppxPackage`
  instructions; remove the `--winappsdk_version` / `--winappsdk_register_provider`
  flag tables; update Implementation notes to describe the
  `LoadLibraryExW` + `WinMLEpCatalog` flow and the renamed bootstrap files.

---

## Specific design decisions to call out

- **`onnxruntime.dll` source — locally built.** The PR uses the `onnxruntime.dll`
  shipped inside the WinML NuGet (because PerceptiveShell does not build ORT
  itself). We do build it (`onnxruntime_BUILD_SHARED_LIB=ON` is required), so
  use `$<TARGET_FILE:onnxruntime>`. **Validation requirement (BLOCKER for
  shipping the change but not for this proposal):** verify the WinML EP MSIX
  stack is happy with a different `onnxruntime.dll` than the one in its own
  NuGet. If incompatible, fall back to copying the package's ORT — but at
  that point this perf test stops exercising local ORT changes, so we'd want
  to revisit the value proposition.
- **Don't link `WindowsML::OnnxRuntime`.** The PR explicitly avoids it
  because the loader uses `GetProcAddress`. Same applies here — link only
  `WindowsML::Api`.
- **Drop `MICROSOFT_WINDOWSAPPSDK_ML_DISABLE_AUTOINITIALIZE`.** It targeted
  the WindowsAppSDK.ML package, no longer referenced.
- **`ORT_API_MANUAL_INIT` propagation stays, but flag the cross-target
  mutation risk.** The current cmake mutates shared static libs
  (`onnx_test_runner_common`, `onnxruntime_test_utils`) globally with
  `ORT_API_MANUAL_INIT`. Other test targets (`onnx_test_runner`,
  `onnxruntime_benchmark`, `onnxruntime_test_all`) link those libs without
  matching the define, so an all-build with this option enabled can hit
  `LNK2038` from the `#pragma detect_mismatch` in
  `onnxruntime_cxx_api.h:159-180`. **This is a pre-existing risk in the
  current WASDK target.** This proposal preserves the current behavior; a
  proper fix (split the perf-test-only sources into a separate static lib,
  or apply `ORT_API_MANUAL_INIT` consistently across all targets that link
  the shared libs when the option is enabled) is tracked as a follow-up
  cleanup item.
- **Plugin-EP path in `ort_test_session.cc` is preserved.** Already calls
  `env.GetEpDevices()` + `AppendExecutionProvider_V2`. Compile path now also
  takes the plugin V2 branch via the `registered_plugin_eps` population
  trick.

---

## Risks / open questions

- **Public availability of `Microsoft.Windows.AI.MachineLearning v2.0.297-preview`** —
  verify this exact version is restorable from the feeds in `NuGet.config`
  (or pick the nearest publicly available preview). The CMake-variable
  approach lets us bump it without code changes.
- **NuGet layout assumptions when restored via `add_nuget_packages`** —
  the PR's package layout assumptions (`build/cmake/`, `include/winml/`,
  `runtimes/win-${arch}/native/...`) come from `nugetDL` restoration into
  PerceptiveShell's directory tree. `add_nuget_packages` (from
  `mschofie/NuGetCMakePackage`) may restore into `build/__nuget/...` with
  different casing. Configure-time verification + adjusting the
  `find_package` invocation may be needed. Where possible, use IMPORTED
  target properties (e.g., `$<TARGET_PROPERTY:WindowsML::Api,
  IMPORTED_LOCATION>`) instead of hardcoded `runtimes/win-${arch}/native/`
  paths.
- **`onnxruntime_providers_shared.dll` need.** Local ORT builds it as a
  separate DLL (`cmake/onnxruntime_providers_cpu.cmake:222-229`). Whether
  WinML-discovered EPs require it at runtime needs a `dumpbin /dependents`
  check on a registered EP DLL. The plan is to copy it defensively; if a
  smoke run confirms it isn't needed, drop the copy step.
- **Local ORT vs WinML-package ORT ABI compat** — see "design decisions"
  above. Make this an explicit validation gate.
- **Pipeline definitions** — review confirmed no `.yml`, `.yaml`, `.ps1`,
  or `.bat` files outside `chrisd/` reference
  `winappsdk_onnxruntime_perf_test`, `onnxruntime_BUILD_WINAPPSDK_PERF_TEST`,
  or `BUILD_WINAPPSDK_PERF_TEST`. The cleanup in this branch is therefore
  self-contained.

---

## Validation strategy

1. **Local build** on x64 first (helper script: `chrisd\p-x64.cmd` then
   `chrisd\b.cmd` after they have been updated to the new option / target
   names).
2. **Smoke run** `standalone_winml_perf_test.exe --list_ep_devices` on a
   developer box that has at least one EP MSIX installed (e.g. OpenVINO or
   QNN), confirming:
     - The `WinMLEpCatalog*` calls discover at least one EP
     - `OrtApi::RegisterExecutionProviderLibrary` succeeds
     - `--list_ep_devices` reports the registered EPs (proves the
       wrong-env blocker is actually fixed)
3. **Inference smoke** with `-e openvino` (or whichever EP is locally
   available) on a small ONNX model to confirm end-to-end behavior matches
   pre-conversion.
4. **EP-context compile** (`-C "ep.context_enable|1 ..." -r 1`) followed by
   a run-from-cache, to confirm `CompileEpContextModel()` takes the plugin
   V2 path correctly.
5. **`dumpbin /dependents` on a registered EP DLL** to confirm whether
   `onnxruntime_providers_shared.dll` is required.
6. **Verify cleanup** by running with verbose ORT logging — no errors
   should appear during the unregister sequence (proves we're operating on a
   live env, not a destroyed one).
7. **ARM64 cross-compile** via `chrisd\p-arm.cmd` + `chrisd\b.cmd`.

---

## Out of scope

- Changes to other PerceptiveShell-style binaries that don't exist in this
  repo (e.g. `test_phi_dll`).
- Removing any non-perf-test consumers of WASDK in this repo (none were
  found in the explored area).
- Pipeline YAML updates (review confirmed none exist outside `chrisd/`).
- Fixing the pre-existing `ORT_API_MANUAL_INIT` cross-target mutation risk
  — tracked as a separate follow-up.

---

## Review history

This proposal incorporates findings from a fresh-context rubber-duck
review (3 blockers and 6 important issues). The critical changes from the
first draft were:

- **(blocker)** Retain a global `OrtGetApiBase()` shim — removing it would
  cause unresolved-symbol link errors in `main.cc`.
- **(blocker)** Bootstrap registers EPs into the **main** `OrtEnv` (passed
  as a parameter), not a private one — `OrtApi::RegisterExecutionProviderLibrary`
  is env-scoped, and `ort_test_session.cc` reads from the main env.
- **(blocker)** Cleanup via `gsl::finally` inside `real_main` instead of in
  `wmain` — by the time `wmain` ran, the env was already destroyed.
- **(important)** Populate `test_config.registered_plugin_eps` from the
  catalog enumeration so `CompileEpContextModel()` uses the plugin V2
  branch.
- **(important)** Add the `10.0.26100.0` Windows SDK platform-version
  guard.
- **(important)** Copy `onnxruntime_providers_shared.dll` (subject to
  dumpbin verification).
- **(important)** Retain `app.manifest` `supportedOS` block and
  `--required_device_type` flag (both initially proposed for removal).
- **(important)** Document the existing `ORT_API_MANUAL_INIT` cross-target
  mutation risk explicitly as a known pre-existing issue / follow-up.
