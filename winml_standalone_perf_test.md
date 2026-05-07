# `winml_standalone_perf_test`

A Windows-only variant of `onnxruntime_perf_test` that loads ONNX Runtime via
the standalone Windows ML flat-C `WinMLEpCatalog` API from the
[`Microsoft.Windows.AI.MachineLearning`](https://www.nuget.org/packages/Microsoft.Windows.AI.MachineLearning)
NuGet package. Execution-provider plugins (QNN, OpenVINO, NVIDIA TensorRT
RTX, Vitis AI, …) are discovered and registered through the catalog at
runtime instead of being linked into ORT directly.

The EXE has no dependency on the WindowsAppSDK bootstrap or WinRT activation,
so it runs on any Windows machine that has the WinML and ONNX Runtime DLLs
sitting next to the executable.

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
  -Donnxruntime_BUILD_WINML_STANDALONE_PERF_TEST=ON ^
  -Donnxruntime_USE_DML=OFF ^
  -Donnxruntime_USE_WINML=OFF
```

For ARM64, swap `-A x64` for `-A ARM64`.

Required CMake options:

| Option | Required value | Notes |
| --- | --- | --- |
| `onnxruntime_BUILD_WINML_STANDALONE_PERF_TEST` | `ON` | Enables this target. |
| `onnxruntime_BUILD_SHARED_LIB` | `ON` | The standalone EXE loads `onnxruntime.dll` from its own directory at runtime. |
| `onnxruntime_BUILD_UNIT_TESTS` | `ON` | The target reuses `onnx_test_runner_common` / `onnxruntime_test_utils`. |

`onnxruntime_USE_CUDA`, `onnxruntime_USE_NV`, and `onnxruntime_USE_TENSORRT`
must be `OFF` — the WinML flat-C catalog owns provider loading and conflicts
with an in-process CUDA/TensorRT EP.

### 2. Build

```cmd
cmake --build build --config RelWithDebInfo --target winml_standalone_perf_test
```

The output is `build\RelWithDebInfo\winml_standalone_perf_test.exe`. The post-
build step copies the runtime dependencies next to the EXE:

* `Microsoft.Windows.AI.MachineLearning.dll`
* `onnxruntime.dll`
* `DirectML.dll`

### NuGet packages

`cmake/winml_standalone_perf_test.cmake` fetches the following package via
[`NuGetCMakePackage`](https://github.com/mschofie/NuGetCMakePackage):

| Package | Version |
| --- | --- |
| `Microsoft.Windows.AI.MachineLearning` | `2.0.297-preview` |

### ORT API version contract

The repo headers (`include/onnxruntime/core/session/onnxruntime_c_api.h`)
define `ORT_API_VERSION` (currently `25`). The bundled NuGet package must
ship an `onnxruntime.dll` that supports **at least** that API version. If
the runtime is older, the EXE will fail at startup with a clear error
message rather than silently fall back to an older API surface — falling
back would risk dereferencing past the runtime's actual struct layout if
any v25-only API is later called.

If the NuGet package is upgraded to a version that ships a *newer* runtime,
no source change is required.

---

## Run

`winml_standalone_perf_test.exe` accepts the same flags as
`onnxruntime_perf_test` plus the following standalone-WinML-specific flag:

| Flag | Default | Description |
| --- | --- | --- |
| `--winml_register_provider <name[,name…]>` | *(empty: register all)* | Restrict provider registration to the listed EP names (exact match). Get names via `--list_ep_devices` (e.g. `OpenVINOExecutionProvider`). If any requested name fails to register, the EXE will exit non-zero rather than silently fall back to CPU-only. |
| `--required_device_type <cpu\|gpu\|npu>` | *(unset)* | Only run on a device of the given type. |

### List available EP devices

```cmd
build\RelWithDebInfo\winml_standalone_perf_test.exe --list_ep_devices
```

### QNN (NPU) — create and reuse an EP-context cache

```cmd
:: Step 1: create cache
build\RelWithDebInfo\winml_standalone_perf_test.exe ^
  -e qnn --required_device_type npu ^
  -i "htp_performance_mode|burst soc_model|60 htp_graph_finalization_optimization_mode|3" ^
  -C "ep.context_enable|1 ep.context_file_path|C:\models\out\model.cache.onnx" ^
  -r 1 -I "C:\models\in\model.onnx"

:: Step 2: run from cache
build\RelWithDebInfo\winml_standalone_perf_test.exe ^
  -e qnn --required_device_type npu ^
  -i "htp_performance_mode|burst soc_model|60 htp_graph_finalization_optimization_mode|3" ^
  -C "ep.context_enable|1" ^
  -t 10 -I "C:\models\out\model.cache.onnx"
```

### OpenVINO (NPU)

```cmd
build\RelWithDebInfo\winml_standalone_perf_test.exe ^
  -e openvino --required_device_type npu ^
  -r 1 -C "ep.context_enable|1 ep.context_embed_mode|0 ep.context_file_path|C:\models\out\model.cache.onnx" ^
  -I C:\models\in\model.onnx

build\RelWithDebInfo\winml_standalone_perf_test.exe ^
  -e openvino --required_device_type npu ^
  -t 10 -I C:\models\out\model.cache.onnx
```

### NVIDIA TensorRT RTX (GPU)

```cmd
build\RelWithDebInfo\winml_standalone_perf_test.exe ^
  -e nvtensorrtrtx --required_device_type gpu ^
  -t 10 -I "C:\models\in\model.onnx"
```

---

## Helper scripts

A set of convenience batch scripts lives under `chrisd\`. They wrap the
configure / build / run commands above for the common scenarios. They assume
you run them from the repo root in a **Developer Command Prompt for VS 2022**
(or an equivalent `vcvarsall` environment) and that the model lives at
`C:\models\in\<model>.onnx`.

### Configure + build

| Script | What it does |
| --- | --- |
| `chrisd\p-x64.cmd` | `cmake -B build` for x64 with `onnxruntime_BUILD_WINML_STANDALONE_PERF_TEST=ON` and most other EPs/options off. |
| `chrisd\p-arm.cmd` | Same as above for ARM64 (cross-compile). |
| `chrisd\b.cmd` | `cmake --build build --config RelWithDebInfo --target winml_standalone_perf_test`. |

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
| `chrisd\go-all.cmd` | `--list_ep_devices` smoke test. |
| `chrisd\simple-intel-test-ape.cmd` | Minimal `-e openvino --required_device_type cpu` smoke test. |
| `chrisd\go.cmd` | Two NVIDIA TensorRT RTX runs — one default, one with `--winml_register_provider NvTensorRTRTXExecutionProvider` to demonstrate selective registration. |

### Misc

* `chrisd\go-cmake-logs.cmd` — re-run CMake configure with logs piped to a
  file for triage.
* `chrisd\copy-perf-test.cmd [BuildType] [Arch]` — copies the built
  `winml_standalone_perf_test.exe` and its `.pdb` into a dated
  `<date>-<commitish>` folder (handy for archiving builds).
* `chrisd\help.txt`, `chrisd\target_includes*.txt`, `chrisd\arm.txt`,
  `chrisd\x64.txt`, `chrisd\target-*.json` — captured `--help`, include
  graphs, and CMake target dumps kept for reference.

---

## Implementation notes

* Entry point is `onnxruntime/test/perftest/main.cc`. The standalone-WinML-
  specific code paths are gated by `BUILD_WINML_STANDALONE_PERF_TEST`.
* The catalog/registration logic lives in
  `onnxruntime/test/perftest/windows/winml_standalone.{h,cc}`. Those two
  files are excluded from the regular `onnxruntime_perf_test` target via a
  `list(FILTER … EXCLUDE REGEX)` in `cmake/onnxruntime_unittests.cmake`,
  because they pull in the WinML NuGet headers.
* `winml_standalone.cc` redefines `OrtGetApiBase` to dynamically load
  `onnxruntime.dll` from the EXE directory (rather than statically linking
  the import lib). This intentionally ignores `PATH` / `SetDllDirectory` so
  the bundled WinML NuGet package owns the runtime version.
* `app.manifest` (`onnxruntime/test/perftest/windows/app.manifest`) sets per-
  monitor DPI awareness and the `supportedOS` GUID.
* `ORT_API_MANUAL_INIT` is propagated to `onnx_test_runner_common` and
  `onnxruntime_test_utils` so the whole link unit agrees with the late
  `Ort::InitApi(...)` call (enforced by `#pragma detect_mismatch` in
  `onnxruntime_cxx_api.h`).
