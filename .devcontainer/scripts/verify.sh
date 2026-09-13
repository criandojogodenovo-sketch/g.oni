#!/usr/bin/env bash
# =============================================================================
# Verificação do devcontainer (§12): falha o provisionamento se qualquer
# ferramenta essencial estiver ausente ou em versão menor que a mínima.
# =============================================================================
set -euo pipefail

RED="\033[31m"; GREEN="\033[32m"; BOLD="\033[1m"; RESET="\033[0m"
FAILURES=0

check_tool() {
    local tool="$1"; shift
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo -e "${RED}[FALHA]${RESET} ${tool}: não encontrado no PATH"
        FAILURES=$((FAILURES + 1))
        return 1
    fi
    echo -e "${GREEN}[OK]${RESET} ${tool}: encontrado"
}

# awk-friendly: extrai "MAJOR.MINOR" e compara numericamente.
version_at_least() {
    local got="$1"; local want="$2"
    local g_maj g_min w_maj w_min
    g_maj=$(echo "${got}" | cut -d. -f1)
    g_min=$(echo "${got}" | cut -d. -f2)
    w_maj=$(echo "${want}" | cut -d. -f1)
    w_min=$(echo "${want}" | cut -d. -f2)
    if [ "${g_maj}" -gt "${w_maj}" ] || { [ "${g_maj}" -eq "${w_maj}" ] && [ "${g_min}" -ge "${w_min}" ]; }; then
        return 0
    fi
    return 1
}

# --- cmake >= 3.28 -----------------------------------------------------------
check_tool cmake
CMAKE_VERSION="$(cmake --version | head -n1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n1)"
if ! version_at_least "${CMAKE_VERSION}" "3.28"; then
    echo -e "${RED}[FALHA]${RESET} cmake ${CMAKE_VERSION} < 3.28"
    FAILURES=$((FAILURES + 1))
else
    echo -e "${GREEN}[OK]${RESET} cmake ${CMAKE_VERSION} >= 3.28"
fi

# --- ninja ---------------------------------------------------------------------
check_tool ninja

# --- g++ >= 13 (C++20 completo: ADR-001 exige GCC 13+/Clang 17+) ---------------
check_tool g++
GXX_VERSION="$(g++ -dumpversion)"
if ! version_at_least "${GXX_VERSION}" "13"; then
    echo -e "${RED}[FALHA]${RESET} g++ ${GXX_VERSION} < 13"
    FAILURES=$((FAILURES + 1))
else
    echo -e "${GREEN}[OK]${RESET} g++ ${GXX_VERSION} >= 13"
fi

# --- gráficos (toolchain de shaders — FASE 3.5) --------------------------------
check_tool glslangValidator
check_tool spirv-val

# --- android --------------------------------------------------------------------
check_tool adb

echo
if [ "${FAILURES}" -ne 0 ]; then
    echo -e "${RED}${BOLD}verify.sh: ${FAILURES} verificação(ões) falharam.${RESET}"
    exit 1
fi
echo -e "${GREEN}${BOLD}verify.sh: ambiente completo para a FASE 1.${RESET}"
