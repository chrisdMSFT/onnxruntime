# ORT Perf Test

onnxruntime_add_executable(onnxruntime_perf_test

```bash

build --config RelWithDebInfo --update

```

## build

```bash

cmake --build G:\onnxruntime\build\Windows\RelWithDebInfo --config RelWithDebInfo --target onnxruntime_perf_test

```
build\Windows\RelWithDebInfo\RelWithDebInfo\onnxruntime_perf_test.exe --help

X:\onnxruntime-testdata

"C:\Users\chrisd\Downloads\Drop.App\lib\net45\drop.exe" get -a -s https://aiinfra.artifacts.visualstudio.com/DefaultCollection -n Lotus/testdata/onnx/model/16 -d x:\testdata

## xx

cmake --build G:\onnxruntime\build\Windows\RelWithDebInfo --config RelWithDebInfo --target onnxruntime

include/onnxruntime/core/session/onnxruntime_ep_device_ep_metadata_keys.h



G:\onnxruntime\build\Windows\RelWithDebInfo\RelWithDebInfo\onnxruntime_perf_test -r 10000 "X:\onnxruntime-testdata\modelzoo\test_squeezenet\model.onnx"

Session creation time cost: 0.0183481 s
First inference time cost: 2 ms
Total inference time cost: 9.40745 s
Total inference requests: 10000
Average inference time cost: 0.940745 ms
Total inference run time: 9.41113 s
Number of inferences per second: 1062.57
Avg CPU usage: 74 %
Peak working set size: 50905088 bytes
Avg CPU usage:74
Peak working set size:50905088
Runs:10000
Min Latency: 0.0007726 s
Max Latency: 0.0018647 s
P50 Latency: 0.0009458 s
P90 Latency: 0.0010243 s
P95 Latency: 0.0010686 s
P99 Latency: 0.0012057 s
P999 Latency: 0.00136 s


https://github.com/gim-home/chwarr-basic-winml-cmake-selfcontained
