@echo off

rem copy G:\onnxruntime-chrisd\build\RelWithDebInfo\winml_standalone_perf_test.exe X:\wcr.cert
rem build\RelWithDebInfo\winml_standalone_perf_test.exe --list_ep_devices

build\RelWithDebInfo\winml_standalone_perf_test -e nvtensorrtrtx --required_device_type gpu -t 10 -I C:\models\in\PSD1.quant.onnx
build\RelWithDebInfo\winml_standalone_perf_test --winml_register_provider NvTensorRTRTXExecutionProvider -e nvtensorrtrtx --required_device_type gpu -t 10 -I C:\models\in\PSD1.quant.onnx
