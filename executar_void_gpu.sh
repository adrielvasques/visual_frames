#!/usr/bin/env bash
# Compila (se preciso) e abre o renderer. VISUAL_FRAMES_DEBUG=1 inicia no modo debug.
set -euo pipefail
cd "$(dirname "$0")/of_void_cloud"

# Silencioso quando dá certo; se falhar, repete para mostrar o erro.
make -j"$(nproc)" >/dev/null 2>&1 || make
cd bin
exec ./of_void_cloud "$@"
