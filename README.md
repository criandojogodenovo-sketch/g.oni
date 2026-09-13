# eng — Engine de Jogos

[![CI Linux](https://github.com/criandojogodenovo-sketch/g.oni/actions/workflows/ci-linux.yml/badge.svg)](https://github.com/criandojogodenovo-sketch/g.oni/actions/workflows/ci-linux.yml)

Engine de jogos 3D escrita em **C++20**, com editor web (TypeScript) e alvo
principal **Android** (Vulkan 1.3, GLES 3.2 como compatibilidade).
Projeto conduzido por fases com contrato técnico formal; este repositório
está na **FASE 1** (núcleo fundacional).

## Estado — FASE 1 (concluída)

- [x] Build: CMake ≥ 3.28 + CMakePresets v6 + Ninja (ADR-003)
- [x] `eng::core` — `Result<T,E>` (sem exceções, ADR-004), `Error`, `Span`, `Version`
- [x] `eng::math` — `Vec2/3/4`, `Mat4` (column-major, inversa via adjugata),
      `Quat` (slerp, Euler, matriz), `Transform` (TRS, composição, decomposição)
- [x] `eng::mem` — `Allocator`, `HeapAllocator` (rastreio de vazamentos),
      `ArenaAllocator` (bump + cabeçalho de tamanho + rewind)
- [x] `eng::log` — `Logger` (níveis, sinks, thread-safe), `ConsoleSink`,
      formatação `{}` sem exceções, macros `ENG_*` com categoria por TU
- [x] Testes: 4 executáveis Catch2 v3.5.2 — **542 asserções**,
      100% verdes com ASan+UBSan+LSan e `-Werror` (debug e release/LTO)
- [x] CI: GitHub Actions (linux-debug + linux-release)
- [x] Devcontainer com camadas idempotentes (base/graphics/android) + verify

## Pré-requisitos

CMake ≥ 3.28, Ninja, GCC ≥ 13 e acesso à rede (FetchContent baixa Catch2 na
primeira configuração). Ou simplesmente abra o repositório em um CodeSpace —
o devcontainer provisiona tudo.

## Comandos (canônicos)

```bash
cmake --preset linux-debug            # configura (Ninja, ASan+UBSan, Werror)
cmake --build --preset linux-debug    # compila
ctest --preset linux-debug --output-on-failure   # testa

cmake --preset linux-release && \
cmake --build --preset linux-release && \
ctest --preset linux-release --output-on-failure  # release com LTO
```

Detalhes completos: [docs/build.md](docs/build.md).

## Estrutura (FASE 1)

```
├── .devcontainer/         # camadas base/graphics/android + verify.sh
├── .github/workflows/     # ci-linux.yml
├── cmake/                 # EngineOptions/Warnings/Sanitizers/Dependencies
├── CMakeLists.txt         # raiz (C++20, módulos de política)
├── CMakePresets.json      # linux-debug | linux-release
├── docs/
│   ├── architecture/00-overview.md   # grafo de dependências (normativo)
│   └── build.md
└── engine/
    ├── core/              # Result, Error, Span, Version
    ├── math/              # Vec2/3/4, Mat4, Quat, Transform
    ├── mem/               # Allocator, Heap, Arena
    └── log/               # Logger, sinks, format, macros
```

Cada módulo segue `include/eng/<módulo>/`, `src/`, `tests/` e expõe o alvo
CMake `eng::<módulo>` (estático, `-fno-exceptions -fno-rtti`).

## Arquitetura

Convenções: namespace `eng::`; matemática column-major, right-handed, ângulos
em radianos (profundidade GL `[-1,1]` por padrão, Vulkan `[0,1]` via
`ENG_MATH_VULKAN_DEPTH=ON`); erros por valor com `Result` (sem exceções no
runtime); logging com categoria declarada por arquivo
(`ENG_LOG_CATEGORY("rhi")` + `ENG_INFO("... {} ...", valor)`).

Grafo de dependências e regras de camadas: [docs/architecture/00-overview.md](docs/architecture/00-overview.md).

## Roadmap

| Fase | Escopo |
|---|---|
| 1 ✅ | core, math, mem, log + build/CI/devcontainer |
| 2 | reflect, events, jobs, ecs, scene |
| 3 | platform, fs, assets (JSON5 → cook) |
| 3.5 | toolchain de shaders (glslang + spirv-val + SPIRV-Cross) |
| 4 | rhi + backend Vulkan (validado em dispositivo/emulador) |
| 5 | backend GL/GLES |
| 6 | física (Jolt), áudio (miniaudio), Android (JNI/APK) |
| 7+ | scripting (Lua), runtime, editor web |
