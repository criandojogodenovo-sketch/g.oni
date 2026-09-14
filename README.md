# eng — Engine de Jogos

[![CI Linux](https://github.com/criandojogodenovo-sketch/g.oni/actions/workflows/ci-linux.yml/badge.svg)](https://github.com/criandojogodenovo-sketch/g.oni/actions/workflows/ci-linux.yml)

Engine de jogos 3D escrita em **C++20**, com alvo principal **Android**
(Vulkan 1.3, GLES 3.2 como compatibilidade).
Projeto conduzido por fases com contrato técnico formal; este repositório
está na **FASE 2** (núcleo fundacional + metadados, eventos, jobs, ECS e cena
concluídos).

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

## Estado — FASE 2 (concluída)

- [x] `eng::reflect` — `TypeRegistry` + macros `ENG_REFLECT` (ADR-021):
      nome canônico/size/align/propriedades (offset+tipo)/enums com
      enumeradores; TypeId FNV-1a 64 determinístico; registro idempotente;
      leituras concorrentes testadas; lookup ausente → nullptr
- [x] `eng::events` — `EventBus` + `Subscription` RAII (ADR-022): ordem de
      inscrição determinística; cancelamento seguro DURANTE dispatch;
      reentrância; handler type-erased com small-buffer — sem std::function;
      publish nunca aloca
- [x] `eng::jobs` — `JobSystem` work-stealing (ADR-023): deque por worker
      (pop LIFO, roubo FIFO circular), `JobHandle::wait`/`waitAll`;
      contabilidade linearizável; shutdown que DRENA e faz join (idempotente);
      propagação de exceções via `ENG_JOBS_CATCH_EXCEPTIONS` (debug ON /
      release OFF); TSan limpo
- [x] `eng::ecs` — `World` sparse-set (ADR-024): `Entity{index,generation}`
      com reciclagem; handles obsoletos são no-op seguro; `each<Ts...>`
      const-correct com snapshot mutável-seguro; componentes move-only
- [x] `eng::scene` — hierarquia de nós sobre o ECS (ADR-025): floresta de
      raízes; attach com detecção de ciclos; destroyNode em cascata;
      transforms locais + matrizes mundo (sob demanda O(prof.) e em lote
      iterativo com cache)
- [x] Testes FASE 2: 5 novos executáveis — **81 casos / ~5.000 asserções**,
      100% verdes com sanitizers e `-Werror`
- [x] 5 novos ADRs (021–025) + docs de arquitetura por módulo

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

## Estrutura (FASE 2)

```
├── .devcontainer/         # camadas base/graphics/android + verify.sh
├── .github/workflows/     # ci-linux.yml
├── cmake/                 # EngineOptions/Warnings/Sanitizers/Dependencies
├── CMakeLists.txt         # raiz (C++20, módulos de política)
├── CMakePresets.json      # linux-debug | linux-release
├── docs/
│   ├── adr/               # ADR-021…025 (decisões da FASE 2)
│   ├── architecture/      # 00-overview + 02…06 por módulo
│   └── build.md
└── engine/
    ├── core/              # Result, Error, Span, Version
    ├── math/              # Vec2/3/4, Mat4, Quat, Transform
    ├── mem/               # Allocator, Heap, Arena
    ├── log/               # Logger, sinks, format, macros
    ├── reflect/           # TypeRegistry + macros ENG_REFLECT
    ├── events/            # EventBus + Subscription RAII
    ├── jobs/              # JobSystem work-stealing
    ├── ecs/               # World sparse-set + Entity geracional
    └── scene/             # hierarquia de nós + transforms
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
| 2 ✅ | reflect, events, jobs, ecs, scene |
| 3 | platform, fs, assets (JSON5 → cook) |
| 3.5 | toolchain de shaders (glslang + spirv-val + SPIRV-Cross) |
| 4 | rhi + backend Vulkan (validado em dispositivo/emulador) |
| 5 | backend GL/GLES |
| 6 | física (Jolt), áudio (miniaudio), Android (JNI/APK) |
| 7+ | scripting (Lua), runtime, editor web |
