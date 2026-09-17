# eng — Engine de Jogos

[![CI Linux](https://github.com/criandojogodenovo-sketch/g.oni/actions/workflows/ci-linux.yml/badge.svg)](https://github.com/criandojogodenovo-sketch/g.oni/actions/workflows/ci-linux.yml)

Engine de jogos **mobile-first** escrita em **C++20**, com alvo principal
**Android** (Vulkan 1.3, GLES 3.2 como compatibilidade).
Projeto conduzido por fases com contrato técnico formal; este repositório
está na **FASE 8** — fases 1–7 concluídas (core/math/mem/log →
reflect/events/jobs/ecs/scene → fs/platform/serial/assets/project →
`eng::rhi` → backends Vulkan/GLES reais → runtime Android com APK) e a
FASE 8 entrega o **Native Mobile Editor** (`editor/` C++ +
`EditorActivity`/`EditorJni` no APK).

## Estado — FASE 1 (concluída)

- [x] Build: CMake ≥ 3.28 + CMakePresets v6 + Ninja (ADR-003)
- [x] `eng::core` — `Result<T,E>` (sem exceções, ADR-004), `Error`, `Span`,
      `Version` e, na FASE 3, `Uuid128` (ADR-028)
- [x] `eng::math` — `Vec2/3/4`, `Mat4` (column-major, inversa via adjugata),
      `Quat` (slerp, Euler, matriz), `Transform` (TRS, composição, decomposição)
- [x] `eng::mem` — `Allocator`, `HeapAllocator` (rastreio de vazamentos),
      `ArenaAllocator` (bump + cabeçalho de tamanho + rewind)
- [x] `eng::log` — `Logger` (níveis, sinks, thread-safe), `ConsoleSink`,
      formatação `{}` sem exceções, macros `ENG_*` com categoria por TU

## Estado — FASE 2 (concluída)

- [x] `eng::reflect` — `TypeRegistry` + macros `ENG_REFLECT` (ADR-021):
      nome canônico/size/align/propriedades (offset+tipo)/enums com
      enumeradores; TypeId FNV-1a 64 determinístico; registro idempotente;
      leituras concorrentes testadas; lookup ausente → nullptr
- [x] `eng::events` — `EventBus` + `Subscription` RAII (ADR-022)
- [x] `eng::jobs` — `JobSystem` work-stealing (ADR-023); TSan limpo
- [x] `eng::ecs` — `World` sparse-set com handles geracionais (ADR-024)
- [x] `eng::scene` — hierarquia de nós + transforms (ADR-025)

## Estado — FASE 3 (concluída)

- [x] `eng::fs` — `Path` (wrapper de std::filesystem com anti-traversal
      `isWithin`), `File` RAII, `FileSystem` abstrato com `NativeFileSystem`
      (error_code, sem exceções) e `MemoryFileSystem` paritário para testes
      (ADR-027)
- [x] `eng::platform` — `PlatformInfo`/`PlatformPaths` (XDG)/`Environment`/
      `ProcessInfo`; único módulo com `#ifdef` por SO; depende de fs, nunca
      o contrário (ADR-026)
- [x] `eng::serial` — JSON puro via nlohmann/json v3.11.3 confinado às vias
      sem exceções (wrapper `JsonValue`), envelope binário `GONI` com
      CRC-32, infraestrutura de migrations (nenhuma ativa) e `StructCodec`
      reflect-driven com codecs de campo por nome de tipo (ADR-030/031)
- [x] `eng::assets` — `AssetId` UUIDv4 de 128 bits (100k gerações sem
      colisão — teste real), `AssetRegistry` determinístico,
      `AssetResolver` com rejeição de path traversal/absoluto,
      `AssetManager` single-threaded com handles shared_ptr e loaders
      type-erased sem RTTI; tipos reservados declarados sem loader
      (ADR-028/029). **Sem async** — FASE 4 via eng::jobs
- [x] `eng::project` — `project.goni.json` com validação ativa de paths
      relativos (absoluto → rejeitado); `ProjectPaths` computa raízes do
      diretório do arquivo (mover o projeto não invalida nada) (ADR-032)
- [x] Serialização de Scene/ECS (dentro de eng::scene — ADR-033):
      `SceneEntityId` UUIDv4 persistente (handles de runtime nunca
      atravessam o disco), componentes via reflect por NOME, ordem
      determinística, round-trip byte-a-byte, referência de asset quebrada
      não impede o load
- [x] Testes: **15 executáveis** (14 unitários + 1 integração e2e em
      `tests/`), 100% verdes com ASan+UBSan+LSan e `-Werror` (debug e
      release/LTO)
- [x] ADRs 026–034 + auditoria e design da fase
      (`docs/phase3_audit.md`, `docs/phase3_design.md`)

## Estado — FASE 4 a 7 (concluídas; resumo)

- [x] **FASE 4** — `eng::rhi`: abstraction de hardware (~25 ops por
      `RhiBackend`), `Renderer` com seleção/validação completa
      (ADR-035/036), `Frame` RAII move-only, handles opacos geracionais
- [x] **FASE 5** — backend Vulkan real (loader dlopen, layers Khronos,
      swapchain, staging, 2-in-flight; ADR-037)
- [x] **FASE 6** — backend OpenGL ES real (EGL surfaceless/pbuffer,
      GLSL compilado em runtime, pixel verificado; ADR-038)
- [x] **FASE 7** — runtime Android: JNI mínima (`GoniJni.cpp`), Activity
      + lifecycle + surface/ANativeWindow com ownership auditado, APK
      arm64-v8a zero permissões (ADRs 039–041); triangle REAL no Linux
      com backends reais; emulador/dispositivo UNAVAILABLE no ambiente

## Estado — FASE 8 (concluída) — Native Mobile Editor

- [x] `editor/` — núcleo C++ do editor (ADR-042):
      `EditorDocument` (projeto/cena/entidades/seleção/play-stop),
      `Inspector` reflect-driven (ADR-043), `AssetBrowser`
      (registry×disco), `Viewport` (câmera 2D/hit-test),
      `ViewportRenderer` (RHI, VBO dinâmico CPU→clip),
      `EditorHost` (surface/lifecycle FASE 7)
- [x] Separção editor×runtime por clone de serialização (ADR-044):
      edição rejeitada em Play; mutação em Play não vaza para a edição
- [x] `EditorActivity.kt` + `EditorJni.kt`/`EditorJni.cpp` — editor
      touch-first (Views nativos, painéis hierarchy/inspector/assets,
      gestos tap/drag/pinch, SAF import, PLAY/STOP) — launcher do APK
- [x] Mudanças aditivas engine: `eng::scene::Name` (componente de
      domínio persistido), catálogo de componentes com
      `emplaceDefault`/`get`/`getMutable`/`removeFrom`, `eachChild`
      const, `StatusCode::{InvalidState,Internal}`
- [x] Testes: **20 suites** no Linux (19 anteriores + `editor` —
      26 casos/278 asserções contra backends reais), zero warnings;
      APK arm64-v8a BUILT+INSPECTED (launcher `EditorActivity`, 57
      símbolos JNI, zero permissões); sem execução em dispositivo
      (evidência por estágio — §13)

## Pré-requisitos

CMake ≥ 3.28, Ninja, GCC ≥ 13 e acesso à rede (FetchContent baixa Catch2 e
nlohmann/json na primeira configuração). Ou simplesmente abra o
repositório em um CodeSpace — o devcontainer provisiona tudo.

## Comandos (canônicos)

```bash
cmake --preset linux-debug            # configura (Ninja, ASan+UBSan, Werror)
cmake --build --preset linux-debug    # compila
ctest --preset linux-debug --output-on-failure   # testa

cmake --preset linux-release && \
cmake --build --preset linux-release && \
ctest --preset linux-release --output-on-failure  # release com LTO
```

Detalhes completos: [docs/build.md](docs/build.md) · Android: [docs/build-android.md](docs/build-android.md).

## Estrutura (FASE 3)

```
├── .devcontainer/         # camadas base/graphics/android + verify.sh
├── .github/workflows/     # ci-linux.yml (linux-debug + linux-release)
├── cmake/                 # EngineOptions/Warnings/Sanitizers/Dependencies
├── CMakeLists.txt         # raiz (C++20, módulos de política, tests/)
├── CMakePresets.json      # linux-debug | linux-release
├── docs/
│   ├── adr/               # ADR-021…034 (decisões FASE 2+3)
│   ├── architecture/      # 00-overview + 02…12 por módulo
│   ├── phase3_audit.md    # auditoria bloqueante do repo real
│   ├── phase3_design.md   # design por módulo
│   ├── project-layout.md  # layout de projeto esperado (exemplo mínimo)
│   └── roadmap.md         # fases e o que a FASE 4 herda pronto
├── engine/
│   ├── core/              # Result, Error, Span, Version, Uuid128
│   ├── math/              # Vec2/3/4, Mat4, Quat, Transform
│   ├── mem/               # Allocator, Heap, Arena
│   ├── log/               # Logger, sinks, format, macros
│   ├── reflect/           # TypeRegistry + macros ENG_REFLECT
│   ├── events/            # EventBus + Subscription RAII
│   ├── jobs/              # JobSystem work-stealing
│   ├── ecs/               # World sparse-set + Entity geracional
│   ├── scene/             # hierarquia + transforms + SceneSerializer
│   ├── fs/                # Path/File/FileSystem (Native+Memory)
│   ├── platform/          # Info/Paths/Environment/ProcessInfo
│   ├── serial/            # JsonValue/envelope/migrations/StructCodec
│   ├── assets/            # AssetId/Registry/Resolver/Manager/Loaders
│   ├── project/           # ProjectId/Config/Paths/File
│   └── rhi/               # Renderer/Frame + backends vulkan/ e gles/
├── editor/                # FASE 8: núcleo C++ do editor (consumidor)
├── android/               # FASE 7/8: runtime + editor Android (Gradle/APK)
└── tests/                 # integração e2e (compõe scene+assets+project)
```

Cada módulo segue `include/eng/<módulo>/`, `src/`, `tests/` e expõe o alvo
CMake `eng::<módulo>` (estático, `-fno-exceptions -fno-rtti`).

## Arquitetura

Convenções: namespace `eng::`; matemática column-major, right-handed, ângulos
em radianos (profundidade GL `[-1,1]` por padrão, Vulkan `[0,1]` via
`ENG_MATH_VULKAN_DEPTH=ON`); erros por valor com `Result` (sem exceções no
runtime); logging com categoria declarada por arquivo
(`ENG_LOG_CATEGORY("rhi")` + `ENG_INFO("... {} ...", valor)`); identidades
persistidas são UUIDv4 fortes (`AssetId`/`SceneEntityId`/`ProjectId` —
nunca path-hash/content-hash, ADR-028); nenhum caminho absoluto é
persistido em arquivo algum (ADR-032).

Grafo de dependências e regras de camadas:
[docs/architecture/00-overview.md](docs/architecture/00-overview.md).
Auditoria da fase (inclui desvios D1–D5 da especificação):
[docs/phase3_audit.md](docs/phase3_audit.md).

## Roadmap

| Fase | Escopo |
|---|---|
| 1 ✅ | core, math, mem, log + build/CI/devcontainer |
| 2 ✅ | reflect, events, jobs, ecs, scene |
| 3 ✅ | fs, platform, serial, assets, project + serialização de cena |
| 3.5 | toolchain de shaders (glslang + spirv-val + SPIRV-Cross) |
| 4 | renderer abstraction `eng::rhi` (ADR-035/036) — concluída |
| 5 | backend Vulkan real `eng::rhi::vulkan` (ADR-037) — concluída |
| 6 | backend OpenGL ES real `eng::rhi::gles` + paridade (ADR-038) — concluída |
| 7 | runtime Android: JNI mínima, Activity+lifecycle, surface/ANativeWindow, APK arm64-v8a (ADRs 039–041) — concluída |
| 8 | Native Mobile Editor: `editor/` C++ + EditorActivity/EditorJni (ADRs 042–044) — concluída |
| 9 | eng::input (touch/actions) + eng::ui + eng::audio |
| 10 | eng::physics + eng::animation + eng::particles |
| 11 | NI-Script (lexer/parser/VM/bindings) |

Detalhes do que a FASE 4 herda pronto: [docs/roadmap.md](docs/roadmap.md).
