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

## Prerequisites

You need winappsdk installed and probaly a good idea to side-load the EP packages as needed.
Current verison is: Microsoft.WindowsAppSDK.Runtime.1.8.250909001

[2509-11](https://microsoft.sharepoint-df.com/teams/WINPD-AIPlatformTools-AIPlatform-WindowsML/Shared%20Documents/Forms/AllItems.aspx?id=%2Fteams%2FWINPD%2DAIPlatformTools%2DAIPlatform%2DWindowsML%2FShared%20Documents%2FDrops%2F2509%2D11&p=true&ga=1&gaS=17)
[EP for 2509-11](https://microsoft.sharepoint-df.com/teams/WINPD-AIPlatformTools-AIPlatform-WindowsML/Shared%20Documents/Forms/AllItems.aspx?id=%2Fteams%2FWINPD%2DAIPlatformTools%2DAIPlatform%2DWindowsML%2FShared%20Documents%2FDrops%2F2509%2D11%2FEP%20MSIX&viewid=6b9efec1%2D4d2c%2D42e8%2Db2d9%2D6024d3d172e1&p=true&ga=1&gaS=17)

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
