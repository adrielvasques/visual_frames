OF_ROOT = ../vendor/openframeworks

ORT_ROOT = ../vendor/onnxruntime-linux-x64-gpu_cuda13-1.30.0

# -isystem: os headers do OpenCV 4.6 geram warnings de enum em C++20.
PROJECT_CFLAGS = -isystem $(ORT_ROOT)/include -isystem /usr/include/opencv4

# DT_RPATH (--disable-new-dtags) em vez de RUNPATH: o provider CUDA do ONNX
# Runtime é carregado por dlopen e o libcublas dele não tem rpath próprio;
# só o RPATH do executável vale para essas dependências indiretas.
PROJECT_LDFLAGS = -L$(ORT_ROOT)/lib -lonnxruntime \
	-lopencv_core -lopencv_imgproc -lopencv_videoio -lopencv_ximgproc \
	-lpulse-simple -lpulse \
	-Wl,--disable-new-dtags \
	-Wl,-rpath,'$$ORIGIN/../../vendor/onnxruntime-linux-x64-gpu_cuda13-1.30.0/lib:$$ORIGIN/../../vendor/cuda/lib'
