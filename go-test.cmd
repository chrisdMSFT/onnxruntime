install\onnxruntime_perf_test.exe --list_ep_devices
echo CPU
install\onnxruntime_perf_test.exe -r 10000 "C:\Users\chrisd\OneDrive - Microsoft\onnxruntime_perf_test\test_squeezenet\model.onnx" --use_winml_ep --select_ep_devices 4 --plugin_eps OpenVINOExecutionProvider
echo NPU
install\onnxruntime_perf_test.exe -r 10000 "C:\Users\chrisd\OneDrive - Microsoft\onnxruntime_perf_test\test_squeezenet\model.onnx" --use_winml_ep --select_ep_devices 2 --plugin_eps OpenVINOExecutionProvider
echo GPU
install\onnxruntime_perf_test.exe -r 10000 "C:\Users\chrisd\OneDrive - Microsoft\onnxruntime_perf_test\test_squeezenet\model.onnx" --use_winml_ep --select_ep_devices 3 --plugin_eps OpenVINOExecutionProvider
