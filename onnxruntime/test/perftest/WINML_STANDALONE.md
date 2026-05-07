# `winml_standalone_perf_test` — deployment story

> Where each DLL beside `winml_standalone_perf_test.exe` comes from, and
> what the EXE expects at runtime.
>
> See [`winml_standalone_perf_test.md`](../../../winml_standalone_perf_test.md)
> at the repo root for the comprehensive build & run reference (CMake
> options, command-line flags, per-EP examples, etc.).

## What lands next to the EXE

The post-build step in
[`cmake/winml_standalone_perf_test.cmake`](../../../cmake/winml_standalone_perf_test.cmake)
copies a fixed set of DLLs into `$<TARGET_FILE_DIR:winml_standalone_perf_test>`
so the EXE runs in-place from the build output. After a successful build
the directory contains:

| File                                          | Source                                                         | How it gets there                                                                 |
|-----------------------------------------------|----------------------------------------------------------------|-----------------------------------------------------------------------------------|
| `winml_standalone_perf_test.exe`              | This build                                                     | Normal CMake `add_executable` target.                                             |
| `winml_standalone_perf_test.pdb`              | This build                                                     | RelWithDebInfo PDB.                                                               |
| `Microsoft.Windows.AI.MachineLearning.dll`    | `Microsoft.Windows.AI.MachineLearning` NuGet (`WindowsML::Api` import target) | `$<TARGET_PROPERTY:WindowsML::Api,IMPORTED_LOCATION>` → `copy_if_different`.       |
| `onnxruntime.dll`                             | Same NuGet, `${WINML_BINARY_DIR}/onnxruntime.dll`              | `copy_if_different`.                                                              |
| `DirectML.dll`                                | Same NuGet, `${WINML_BINARY_DIR}/DirectML.dll`                 | `copy_if_different`.                                                              |
| Per-EP libraries (e.g. QNN, OpenVINO, NV TensorRT RTX) | Their own NuGet install location, discovered at runtime by the WinML EP catalog | **NOT** copied next to the EXE. Loaded via `WinMLEpGetLibraryPath` + `Ort::Env::RegisterExecutionProviderLibrary` from wherever WinML installed them. |

`WINML_BINARY_DIR` is exported by the
`microsoft.windows.ai.machinelearning` CMake config from the WinML NuGet
package. Configure-time guard in `cmake/winml_standalone_perf_test.cmake`
fails loudly if the variable is unset (e.g. NuGet package layout drift),
rather than emitting a silent post-build copy that does nothing.

## Why `onnxruntime.dll` resolution is EXE-dir only

The standalone EXE intentionally does **not** static-link the
`onnxruntime.dll` import lib. Instead,
[`winml_standalone.cc`](winml_standalone.cc) redefines
`OrtGetApiBase` to dynamically `LoadLibraryExW` the DLL from the EXE
directory and **only** from the EXE directory — `PATH` and
`SetDllDirectory` are ignored.

Why: the bundled WinML NuGet package is the contract owner of the
runtime version. If we honored `PATH`, a developer with a sideloaded
`onnxruntime.dll` earlier on `PATH` would silently bind to a different
runtime build, with no warning, and any version-incompatible API call
would crash inside the C API trampoline.

## Redeploying to another machine

The minimum payload is the four DLLs from the table above plus the EXE.
Copy the entire `build\RelWithDebInfo\` directory to keep things
simple. EP libraries are discovered through the WinML catalog on the
target machine, so they do **not** need to ship next to the EXE — but
the WinML runtime on the target machine must have them installed (e.g.
the QNN package for `--required_device_type npu` on Snapdragon, or the
NVIDIA TensorRT RTX package for `-e nvtensorrtrtx`).

## ORT API version contract

The repo headers (`include/onnxruntime/core/session/onnxruntime_c_api.h`)
define `ORT_API_VERSION`. The runtime DLL bundled with the WinML NuGet
must support **at least** that version. Newer is fine (forward-
compatible struct layout). Older causes the EXE to refuse to start with
a clear error message rather than fall back to an older API surface,
because falling back would risk dereferencing past the runtime's actual
struct layout if any newer-API call exists in the link unit.

## Implementation pointers

- Entry point and version-check fast-fail: [`main.cc`](main.cc)
- Catalog enumeration & registration (RAII):
  [`winml_standalone.cc`](winml_standalone.cc) /
  [`winml_standalone.h`](winml_standalone.h)
- `OrtGetApiBase` redefinition + link-order warning banner:
  [`winml_standalone.cc:23-69`](winml_standalone.cc)
- Glob exclusion that keeps `winml_standalone.{cc,h}` out of the regular
  `onnxruntime_perf_test` target:
  [`cmake/onnxruntime_unittests.cmake`](../../../cmake/onnxruntime_unittests.cmake)
  (search for `winml_standalone`)
