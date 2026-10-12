#!/usr/bin/env bash
# Baixa o que fica em vendor/ (fora do git por tamanho):
#   - openFrameworks 0.12.1 (linux64), já compilando a biblioteca
#   - ONNX Runtime 1.30.0 GPU para CUDA 13
#   - bibliotecas CUDA/cuDNN (só os .so das wheels nvidia-* do PyPI)
# Antes, instale as dependências de sistema (precisa de sudo):
#   sudo apt install libopencv-dev libopencv-contrib-dev libpulse-dev python3-pip
#   e, depois que o openFrameworks for baixado:
#   sudo vendor/openframeworks/scripts/linux/ubuntu/install_dependencies.sh
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p vendor
TEMP=$(mktemp -d)
trap 'rm -rf "$TEMP"' EXIT

if [ ! -d vendor/openframeworks ]; then
    echo "Baixando openFrameworks 0.12.1..."
    curl -L -o "$TEMP/of.tgz" \
        https://github.com/openframeworks/openFrameworks/releases/download/0.12.1/of_v0.12.1_linux64_gcc6_release.tar.gz
    tar -xzf "$TEMP/of.tgz" -C "$TEMP"
    mv "$TEMP"/of_v0.12.1_* vendor/openframeworks
    echo "Compilando a biblioteca do openFrameworks (se falhar, rode antes:"
    echo "  sudo vendor/openframeworks/scripts/linux/ubuntu/install_dependencies.sh)"
    vendor/openframeworks/scripts/linux/compileOF.sh -j"$(nproc)"
fi

ORT=onnxruntime-linux-x64-gpu_cuda13-1.30.0
if [ ! -d "vendor/$ORT" ]; then
    echo "Baixando ONNX Runtime 1.30.0 (CUDA 13)..."
    curl -L -o "$TEMP/ort.tgz" "https://github.com/microsoft/onnxruntime/releases/download/v1.30.0/$ORT.tgz"
    tar -xzf "$TEMP/ort.tgz" -C vendor
    rm -f "vendor/$ORT/lib/libonnxruntime_providers_tensorrt.so"
fi

if [ ! -d vendor/cuda/lib ]; then
    echo "Baixando CUDA runtime, cuBLAS, cuRAND e cuDNN (wheels do PyPI)..."
    pip3 download -q --no-deps -d "$TEMP/wheels" \
        nvidia-cuda-runtime==13.4.92 nvidia-cublas==13.8.1.7 nvidia-curand==10.4.4.72 nvidia-cudnn-cu13==9.27.0.42
    mkdir -p "$TEMP/extraido" vendor/cuda/lib
    for wheel in "$TEMP"/wheels/*.whl; do
        python3 -I -m zipfile -e "$wheel" "$TEMP/extraido/"
    done
    find "$TEMP/extraido" -name '*.so*' -path '*lib*' -exec cp -a {} vendor/cuda/lib/ \;
fi

echo "Pronto. Rode ./executar_void_gpu.sh"
