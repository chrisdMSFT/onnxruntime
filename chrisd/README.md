# chrisd_peftest.cmake

# Basic build

```bash

mkdir build && cd build
cmake .. -DONNXRUNTIME_ROOT=/path/to/onnxruntime
cmake --build .

# With options
cmake .. -DONNXRUNTIME_ROOT=/path/to/onnxruntime -DUSE_CUDA=ON -DBUILD_SHARED_LIBS=OFF

```
