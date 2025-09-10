# onnxruntime_perf_test

## Repo

```Build
git clone -b user/chrisd/perf-test https://github.com/chrisdMSFT/onnxruntime  onnxruntime-chrisdMSFT
```

## Build

```bash
build --update --build_shared_lib --config RelWithDebInfo --disable_memleak_checker
cmake --build c:\git\onnxruntime-chrisd\build\Windows\RelWithDebInfo --config RelWithDebInfo --target onnxruntime_perf_test
```

Note: It really likes absloute paths, so update the prefix to `build\Windows\RelWithDebInfo` as needed.

## Install

```bash
mkdir install
copy build\Windows\RelWithDebInfo\RelWithDebInfo\onnxruntime_perf_test.exe install
copy build\Windows\RelWithDebInfo\RelWithDebInfo\onnxruntime_perf_test.exe.manifest install
copy build\Windows\RelWithDebInfo\RelWithDebInfo\Microsoft.Windows.AI.MachineLearning.dll install
copy build\Windows\RelWithDebInfo\__nuget\Microsoft.WindowsAppSDK.ML.1.8.2088\runtimes-framework\win-x64\native\onnxruntime.dll install
copy build\Windows\RelWithDebInfo\__nuget\Microsoft.WindowsAppSDK.ML.1.8.2088\runtimes-framework\win-x64\native\onnxruntime_providers_shared.dll install
```

## Test

```bash
install\onnxruntime_perf_test.exe --list_ep_devices

echo CPU
install\onnxruntime_perf_test.exe -r 10000 "C:\Users\chrisd\OneDrive - Microsoft\onnxruntime_perf_test\test_squeezenet\model.onnx" --use_winml_ep --select_ep_devices 4 --plugin_eps OpenVINOExecutionProvider

echo NPU
install\onnxruntime_perf_test.exe -r 10000 "C:\Users\chrisd\OneDrive - Microsoft\onnxruntime_perf_test\test_squeezenet\model.onnx" --use_winml_ep --select_ep_devices 2 --plugin_eps OpenVINOExecutionProvider

echo GPU
install\onnxruntime_perf_test.exe -r 10000 "C:\Users\chrisd\OneDrive - Microsoft\onnxruntime_perf_test\test_squeezenet\model.onnx" --use_winml_ep --select_ep_devices 3 --plugin_eps OpenVINOExecutionProvider
```
