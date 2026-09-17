cmake_minimum_required(VERSION 3.28)

# VERSION é a fonte única da verdade do semver do projeto (§8: formatVersion).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" ENG_VERSION_TEXT)
string(STRIP "${ENG_VERSION_TEXT}" ENG_VERSION_TEXT)
if(NOT ENG_VERSION_TEXT MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION inválido: '${ENG_VERSION_TEXT}' (esperado MAJOR.MINOR.PATCH)")
endif()

project(eng
    VERSION ${ENG_VERSION_TEXT}
    DESCRIPTION "eng — engine de jogos (codinome provisório; FASE 1: core, math, mem, log)"
    LANGUAGES CXX
)

# ADR-001: C++20 obrigatório, sem extensões de compilador.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# FASE 1 não usa módulos C++20: a varredura automática do CMake 3.28+ injeta
# -fmodules-ts, que muda a estratégia de emissão de vtables/typeinfo e quebra
# a combinação lib(-fno-rtti) + testes(RTTI p/ Catch2). Desligada até a FASE
# que adotar módulos de fato (decisão registrada no relatório §17.4).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Módulos de apoio do build vivem em cmake/ (§2 — PARTE 2).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(EngineOptions)        # opções globais + eng_add_module()
include(EngineWarnings)       # ADR-020: -Wall -Wextra -Wpedantic [-Werror]
include(EngineSanitizers)     # ASan+UBSan (presets de debug)
include(EngineDependencies)   # FASE 1: Catch2 v3.5.2 · FASE 3: nlohmann/json v3.11.3 (ADR-030)

include(CTest)

add_subdirectory(engine)
# FASE 3 (missão §2.8): integração e2e — pode compor scene+assets+project.
add_subdirectory(tests)
# FASE 7: runtime Android (CONSUMIDOR de engine/) — testado no Linux com
# backends reais; o alvo do APK (libgoni.so) é construído pelo CMake do
# app (android/app/src/main/cpp) a partir dos MESMOS fontes.
if(ENG_BUILD_TESTS)
    add_subdirectory(android/runtime)
endif()

message(STATUS "eng ${PROJECT_VERSION} — FASE 4 (renderer abstraction eng::rhi)")
message(STATUS "Presets canônicos: linux-debug, linux-release — ver docs/build.md")
