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

This folder contains a single model for testing purposes, please consule the docs in the ORT repo for more detials on the model inputs.

```bash
onnxruntime_perf_test --list_ep_devices

echo CPU
onnxruntime_perf_test  -r 10000 "test_squeezenet\model.onnx" --use_winml_ep --select_ep_devices 4 --plugin_eps OpenVINOExecutionProvider

echo NPU
onnxruntime_perf_test  -r 10000 "test_squeezenet\model.onnx" --use_winml_ep --select_ep_devices 2 --plugin_eps OpenVINOExecutionProvider

echo GPU
onnxruntime_perf_test  -r 10000 "test_squeezenet\model.onnx" --use_winml_ep --select_ep_devices 3 --plugin_eps OpenVINOExecutionProvider
```

### Example: onnxruntime_perf_test.exe --list_ep_devices

This is a down and dirty hack of the perf tool, so there are some rough spots.  For now you need to provide the device index and the plugin name.

So if you wanted to test against the OpenVINOExecutionProvider NPU, you would specify device 2 and the OpenVINOExecutionProvider.

```bash
--select_ep_devices 2 --plugin_eps OpenVINOExecutionProvider
```

To get the device index and name, you can run with the `--list_ep_devices` flag, which will also confirm the WinML providers are working and registered. 

```bash
Provider: OpenVINOExecutionProvider Ready state: 1 --> EnsureReadyAsync  --> TryRegister(1)  --> FIN!
```

```text
onnxruntime_perf_test.exe --list_ep_devices
Ort::GetApi() 0x00007FFFE8AFD650
1. GetEpDevices
CPUExecutionProvider
DmlExecutionProvider

Getting available providers...

Provider: OpenVINOExecutionProvider Ready state: 1 --> EnsureReadyAsync  --> TryRegister(1)  --> FIN!
test_config.use_winml_ep: 0
===== device id 0 ======
name: CPUExecutionProvider
vendor: Microsoft
metadata:
  version: 1.23.0

===== device id 1 ======
name: DmlExecutionProvider
vendor: Microsoft
metadata:
  version: 1.23.0

===== device id 2 ======
name: OpenVINOExecutionProvider
vendor: Intel
metadata:
  ov_device: NPU
  version: 1.0.0+8b43c0dd6

===== device id 3 ======
name: OpenVINOExecutionProvider
vendor: Intel
metadata:
  ov_device: GPU
  version: 1.0.0+8b43c0dd6

===== device id 4 ======
name: OpenVINOExecutionProvider
vendor: Intel
metadata:
  ov_device: CPU
  version: 1.0.0+8b43c0dd6

===== device id 5 ======
name: OpenVINOExecutionProvider.AUTO
vendor: Intel
metadata:
  ov_meta_device: AUTO
  ov_device: NPU
  version: 1.0.0+8b43c0dd6

===== device id 6 ======
name: OpenVINOExecutionProvider.AUTO
vendor: Intel
metadata:
  ov_meta_device: AUTO
  ov_device: GPU
  version: 1.0.0+8b43c0dd6

===== device id 7 ======
name: OpenVINOExecutionProvider.AUTO
vendor: Intel
metadata:
  ov_meta_device: AUTO
  ov_device: CPU
  version: 1.0.0+8b43c0dd6

No plugin execution provider libraries are registered. Please specify them using "--plugin_ep_libs"; otherwise, only CPU may be available.
```
