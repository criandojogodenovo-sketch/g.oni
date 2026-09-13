#!/usr/bin/env bash
# =============================================================================
# Camada base do devcontainer (§12): compilador + build system.
# Idempotente: pode rodar quantas vezes for necessário.
# =============================================================================
set -euo pipefail

echo "==> [base] instalando toolchain C++..."
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    curl \
    unzip \
    pkg-config \
    ccache

echo "==> [base] versões instaladas:"
cmake --version | head -n1
ninja --version
g++ --version | head -n1
echo "==> [base] OK"
