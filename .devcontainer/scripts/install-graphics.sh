#!/usr/bin/env bash
# =============================================================================
# Camada gráfica do devcontainer (§12): Vulkan/GL tools + validação SPIR-V.
# Idempotente.
# =============================================================================
set -euo pipefail

echo "==> [graphics] instalando ferramentas gráficas..."
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    libvulkan-dev \
    vulkan-tools \
    glslang-tools \
    spirv-tools \
    libgles2-mesa-dev \
    libgl1-mesa-dev \
    xvfb

echo "==> [graphics] versões instaladas:"
glslangValidator --version | head -n1
spirv-val --version | head -n1
echo "==> [graphics] OK"
