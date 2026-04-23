# `winappsdk_onnxruntime_perf_test`

A Windows-only variant of `onnxruntime_perf_test` that loads ONNX Runtime via
the [Windows App SDK](https://learn.microsoft.com/windows/apps/windows-app-sdk/)
ML runtime and its execution-provider plugins (QNN, OpenVINO, NVIDIA TensorRT
RTX, …) instead of linking ORT directly.

It reuses the existing `onnxruntime_perf_test` argument parser and inference
loop, so most flags work identically; the differences are documented below.

---

## Build

The target is opt-in and only builds on Windows / MSVC.

### 1. Configure

```cmd
cmake -B build ^
  -G "Visual Studio 17 2022" ^
  -A x64 ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -Donnxruntime_BUILD_SHARED_LIB=ON ^
  -Donnxruntime_BUILD_UNIT_TESTS=ON ^
  -Donnxruntime_BUILD_WINAPPSDK_PERF_TEST=ON ^
  -DCPPWINRT_VERSION=2.0.250303.1
```

For ARM64, swap `-A x64` for `-A ARM64`.

Required CMake options:

| Option | Required value | Notes |
| --- | --- | --- |
| `onnxruntime_BUILD_WINAPPSDK_PERF_TEST` | `ON` | Enables this target. |
| `onnxruntime_BUILD_SHARED_LIB` | `ON` | Bootstrap loads ORT through the WinAppSDK ML runtime. |
| `onnxruntime_BUILD_UNIT_TESTS` | `ON` | Temporary; the target reuses `onnx_test_runner_common` / `onnxruntime_test_utils`. |
| `CPPWINRT_VERSION` | e.g. `2.0.250303.1` | Version of `Microsoft.Windows.CppWinRT` to fetch. |

`onnxruntime_USE_CUDA`, `onnxruntime_USE_NV`, and `onnxruntime_USE_TENSORRT`
must be `OFF` — the WinAppSDK ML runtime owns provider loading and conflicts
with an in-process CUDA/TensorRT EP.

### 2. Build

```cmd
cmake --build build --config RelWithDebInfo --target winappsdk_onnxruntime_perf_test
```

The output is `build\RelWithDebInfo\winappsdk_onnxruntime_perf_test.exe`.

### NuGet packages

`cmake/winappsdk_onnxruntime_perf_test.cmake` fetches the following packages
via [`NuGetCMakePackage`](https://github.com/mschofie/NuGetCMakePackage):

| Package | Version |
| --- | --- |
| `Microsoft.Windows.CppWinRT` | `${CPPWINRT_VERSION}` |
| `Microsoft.Windows.ImplementationLibrary` | `1.0.250325.1` |
| `Microsoft.WindowsAppSDK.Runtime` | `2.0.0-experimental3` |
| `Microsoft.WindowsAppSDK.Foundation` | `2.0.8-experimental` |
| `Microsoft.WindowsAppSDK.InteractiveExperiences` | `1.8.251104001` |
| `Microsoft.WindowsAppSDK.ML` | `2.0.44-experimental` |

See [WinAppSDK 2.0 (experimental) downloads](https://learn.microsoft.com/windows/apps/windows-app-sdk/downloads#windows-app-sdk-20-experimental)
for the matching runtimes that must be installed on the test machine.

To check what runtimes are installed locally:

```pwsh
Get-AppxPackage -Name "Microsoft.WindowsAppRuntime.*" |
  Select-Object -ExpandProperty PackageFamilyName -Unique |
  Sort-Object -Descending
```

---

## Run

`winappsdk_onnxruntime_perf_test.exe` accepts the same flags as
`onnxruntime_perf_test` plus the following WinAppSDK-specific flags:

| Flag | Default | Description |
| --- | --- | --- |
| `--winappsdk_version <major.minor>` | `1.8` | The `major.minor` used in the `PackageFamilyName`. e.g. `1.7` binds to `Microsoft.WindowsAppRuntime.1.7_8wekyb3d8bbwe`. |
| `--winappsdk_register_provider <name[,name…]>` | *(empty: register all)* | Restrict provider registration to the listed EP names (exact match). Get names via `--list_ep_devices` (e.g. `OpenVINOExecutionProvider`). |
| `--required_device_type <cpu\|gpu\|npu>` | *(unset)* | Only run on a device of the given type. |

### List available EP devices

```cmd
build\RelWithDebInfo\winappsdk_onnxruntime_perf_test.exe --list_ep_devices
```

### QNN (NPU) — create and reuse an EP-context cache

```cmd
:: Step 1: create cache
build\RelWithDebInfo\winappsdk_onnxruntime_perf_test.exe ^
  -e qnn --required_device_type npu ^
  -i "htp_performance_mode|burst soc_model|60 htp_graph_finalization_optimization_mode|3" ^
  -C "ep.context_enable|1 ep.context_file_path|C:\models\out\model.cache.onnx" ^
  -r 1 -I "C:\models\in\model.onnx"

:: Step 2: run from cache
build\RelWithDebInfo\winappsdk_onnxruntime_perf_test.exe ^
  -e qnn --required_device_type npu ^
  -i "htp_performance_mode|burst soc_model|60 htp_graph_finalization_optimization_mode|3" ^
  -C "ep.context_enable|1" ^
  -t 10 -I "C:\models\out\model.cache.onnx"
```

### OpenVINO (NPU)

```cmd
build\RelWithDebInfo\winappsdk_onnxruntime_perf_test.exe ^
  -e openvino --required_device_type npu ^
  -r 1 -C "ep.context_enable|1 ep.context_embed_mode|0 ep.context_file_path|C:\models\out\model.cache.onnx" ^
  -I C:\models\in\model.onnx

build\RelWithDebInfo\winappsdk_onnxruntime_perf_test.exe ^
  -e openvino --required_device_type npu ^
  -t 10 -I C:\models\out\model.cache.onnx
```

### NVIDIA TensorRT RTX (GPU)

```cmd
build\RelWithDebInfo\winappsdk_onnxruntime_perf_test.exe ^
  -e nvtensorrtrtx --required_device_type gpu ^
  -t 10 -I "C:\models\in\model.onnx"
```

---

## Helper scripts

A set of convenience batch scripts lives under `chrisd\`. They wrap the
configure / build / run commands above for the common scenarios.They assume you run them from the repo root in a
**Developer Command Prompt for VS 2022** (or an equivalent `vcvarsall`
environment) and that the model lives at `C:\models\in\<model>.onnx`.

### Configure + build

| Script | What it does |
| --- | --- |
| `chrisd\p-x64.cmd` | `cmake -B build` for x64 with `onnxruntime_BUILD_WINAPPSDK_PERF_TEST=ON` and most other EPs/options off. |
| `chrisd\p-arm.cmd` | Same as above for ARM64 (cross-compile). |
| `chrisd\b.cmd` | `cmake --build build --config RelWithDebInfo --target winappsdk_onnxruntime_perf_test`. |

Typical flow:

```cmd
chrisd\p-x64.cmd       :: or p-arm.cmd
chrisd\b.cmd
```

### Run per EP

Each `go-*.cmd` script copies a sample model into `C:\models\in\`, lists the
available EP devices, then runs a create-cache + run-from-cache pair against
the named EP. Edit the model path at the top of the script to point at your
own `.onnx` file.

| Script | Flags it exercises |
| --- | --- |
| `chrisd\go-qnn.cmd` | `-e qnn --required_device_type npu` with `htp_performance_mode` set to both `extreme_power_saver` and `burst`, plus `ep.context_*` caching. |
| `chrisd\go-openvino.cmd` | `-e openvino --required_device_type npu` with `ep.context_embed_mode|0` external-cache. |
| `chrisd\go-nvidia-tests.cmd` | `-e nvtensorrtrtx --required_device_type gpu`. |
| `chrisd\go-all.cmd` | `--list_ep_devices` against `--winappsdk_version` `2.0-experimental3`, `1.8`, and `1.7` (handy for checking which runtimes you actually have installed). |
| `chrisd\simple-intel-test-ape.cmd` | Minimal `-e openvino --required_device_type cpu` smoke test. |
| `chrisd\go.cmd` | Two NVIDIA TensorRT RTX runs — one default, one with `--winappsdk_register_provider NvTensorRTRTXExecutionProvider` to demonstrate selective registration. |

### Misc

* `chrisd\go-cmake-logs.cmd` — re-run CMake configure with logs piped to a
  file for triage.
* `chrisd\copy-perf-test.cmd [BuildType] [Arch]` — copies the built
  `winappsdk_onnxruntime_perf_test.exe` and its `.pdb` into a dated
  `<date>-<commitish>` folder (handy for archiving builds).
* `chrisd\help.txt`, `chrisd\target_includes*.txt`, `chrisd\arm.txt`,
  `chrisd\x64.txt`, `chrisd\target-*.json` — captured `--help`, include
  graphs, and CMake target dumps kept for reference.

---

## Implementation notes

* Entry point is `onnxruntime/test/perftest/main.cc` (built into the regular
  perf test as well — WinAppSDK-specific code is gated by
  `BUILD_WINAPPSDK_PERF_TEST`).
* Bootstrap lives in `onnxruntime/test/perftest/windows/winappsdk_bootstrap.{h,cc}`.
  It initializes the WinAppSDK ML runtime, registers the requested EP plugins,
  and tears them down on exit.
* `app.manifest` (`onnxruntime/test/perftest/windows/app.manifest`) sets per-
  monitor DPI awareness and the `supportedOS` GUID required to load WinAppSDK
  packaged binaries.
* `ORT_API_MANUAL_INIT` is propagated to `onnx_test_runner_common` and
  `onnxruntime_test_utils` so the whole link unit agrees with the bootstrap's
  late `Ort::InitApi(...)` call (enforced by `#pragma detect_mismatch` in
  `onnxruntime_cxx_api.h`).
