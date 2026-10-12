#!/usr/bin/env bash
# Compila o benchmark do detector em of_void_cloud/bin/benchmark.
set -euo pipefail
cd "$(dirname "$0")/.."

ORT=../vendor/onnxruntime-linux-x64-gpu_cuda13-1.30.0
mkdir -p bin
g++ -std=c++17 -O2 -Wall -Wextra \
    ferramentas/benchmark.cpp src/DetectorPessoas.cpp \
    -I"$ORT/include" $(pkg-config --cflags opencv4) \
    -L"$ORT/lib" -lonnxruntime \
    -Wl,--disable-new-dtags -Wl,-rpath,'$ORIGIN/../../vendor/onnxruntime-linux-x64-gpu_cuda13-1.30.0/lib:$ORIGIN/../../vendor/cuda/lib' \
    -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_videoio -lpthread \
    -o bin/benchmark
