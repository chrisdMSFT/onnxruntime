@REM build\Debug\onnxruntime_perf_test.exe --list_ep_devices
@REM build\Debug\onnxruntime_perf_test.exe -r 10 -I x:\PSD\PSD1.quant.onnx --select_ep_devices 2

rmdir x:\output /s /q
mkdir x:\output

echo.
echo "List available EP devices"
echo.
build\RelWithDebInfo\onnxruntime_perf_test --list_ep_devices

echo.
echo "Go NVidia!"
echo.
build\RelWithDebInfo\onnxruntime_perf_test -e nvtensorrtrtx --required_device_type gpu -t 100 -I x:\PSD\PSD1.quant.onnx
