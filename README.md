# eng — Engine de Jogos

[![CI Linux](https://github.com/criandojogodenovo-sketch/g.oni/actions/workflows/ci-linux.yml/badge.svg)](https://github.com/criandojogodenovo-sketch/g.oni/actions/workflows/ci-linux.yml)
[![CI Android](https://github.com/criandojogodenovo-sketch/g.oni/actions/workflows/ci-android.yml/badge.svg)](https://github.com/criandojogodenovo-sketch/g.oni/actions/workflows/ci-android.yml)

Engine de jogos **mobile-first** escrita em **C++20**, com alvo principal
**Android** (Vulkan 1.3, GLES 3.2 como compatibilidade). Projeto conduzido
por fases com contrato técnico formal; as **FASES 1–11 estão concluídas** —
a **FASE 12 (Build & Export)** segue no roadmap.

Pilha: **C++20** (engine), **Kotlin** (integração Android), **JNI**
(ponte nativa), **Vulkan/GLES** (rendering), **CMake** (build nativo),
**Gradle** (build Android), **GLSL** (shaders). O editor é o **Native
Mobile Editor** no APK — não há Web Editor, Flutter, Electron ou browser
no escopo, e o scripting será a linguagem própria **NI-Script** (não Lua,
não Python).

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

- [x] `eng::reflect` — `TypeRegistry` + macros `ENG_REFLECT` (ADR-021)
- [x] `eng::events` — `EventBus` + `Subscription` RAII (ADR-022)
- [x] `eng::jobs` — `JobSystem` work-stealing (ADR-023); TSan limpo
- [x] `eng::ecs` — `World` sparse-set com handles geracionais (ADR-024)
- [x] `eng::scene` — hierarquia de nós + transforms (ADR-025)

## Estado — FASE 3 (concluída)

- [x] `eng::fs` — `Path` (anti-traversal `isWithin`), `File` RAII,
      `FileSystem` abstrato com `NativeFileSystem`/`MemoryFileSystem`
      (ADR-027)
- [x] `eng::platform` — `PlatformInfo`/`PlatformPaths` (XDG)/`Environment`/
      `ProcessInfo`; único módulo com `#ifdef` por SO (ADR-026)
- [x] `eng::serial` — JSON via nlohmann/json confinado às vias sem
      exceções, envelope binário `GONI` com CRC-32, migrations,
      `StructCodec` reflect-driven (ADR-030/031)
- [x] `eng::assets` — `AssetId` UUIDv4, `AssetRegistry` determinístico,
      `AssetResolver`/`AssetManager` (ADR-028/029)
- [x] `eng::project` — `project.goni.json` com paths relativos validados
      (ADR-032); serialização de Scene/ECS por nome estável (ADR-033)

## Estado — FASES 4–7 (concluídas; resumo)

- [x] **FASE 4** — `eng::rhi`: abstraction de hardware (~25 ops por
      `RhiBackend`), `Renderer` com seleção/validação completa
      (ADR-035/036), `Frame` RAII move-only, handles opacos geracionais
- [x] **FASE 5** — backend Vulkan real (loader dlopen, layers Khronos,
      swapchain, staging, 2-in-flight; ADR-037)
- [x] **FASE 6** — backend OpenGL ES real (EGL surfaceless/pbuffer,
      GLSL compilado em runtime, pixel verificado; ADR-038)
- [x] **FASE 7** — runtime Android: JNI mínima, Activity + lifecycle +
      surface/ANativeWindow com ownership auditado, APK arm64-v8a zero
      permissões (ADRs 039–041); render no Choreographer/UI-thread como
      decisão documentada (ADR-039); triangle REAL no Linux com backends
      reais; emulador/dispositivo UNAVAILABLE no ambiente

## Estado — FASE 8 (concluída) — Native Mobile Editor

- [x] `editor/` — núcleo C++ do editor (ADR-042):
      `EditorDocument` (projeto/cena/entidades/seleção/play-stop),
      `Inspector` reflect-driven (ADR-043), `AssetBrowser`
      (registry×disco), `Viewport` (câmera 2D/hit-test),
      `ViewportRenderer` (RHI, VBO dinâmico CPU→clip),
      `EditorHost` (surface/lifecycle FASE 7)
- [x] Separação editor×runtime por clone de serialização (ADR-044):
      edição rejeitada em Play; mutação em Play não vaza para a edição
- [x] `EditorActivity.kt` + `EditorJni.kt`/`EditorJni.cpp` — editor
      touch-first (Views nativos, painéis hierarchy/inspector/assets,
      gestos tap/drag/pinch, SAF import, PLAY/STOP) — launcher do APK
- [x] Auditoria final 4–10 corrigiu a integração de input do jogo em
      Play: eventos brutos com pointer ID real + viewport do jogo com
      tamanho real (zonas de toque funcionais no dispositivo)

## Estado — FASE 9 (concluída) — Input + UI + Audio

- [x] `eng::input` (ADR-045): TouchState por pointer ID (down/move/up/
      multitouch/pressão/delta por janela — corrigido na auditoria
      final), teclado canônico, `InputSystem` com janela por frame,
      ações por combinação de fontes, bindings de/para JSON
- [x] `eng::ui` (ADR-046): widgets, layout por anchors +
      design-resolution, hit-test top-most, draw-list de quads (SEM
      RHI — o host desenha), fonte 5×7 pontilhada
- [x] `eng::audio` (ADR-047): WAV PCM8/16/24/32f, mixer software f32
      pull, vozes geracionais, Music por janelas de 16k frames sobre o
      arquivo em memória (decode por janela), backend AAudio via dlopen
      (API<26 → erro preciso) + Null para testes
- [x] Integração: `GoniActivity`→JNI→InputSystem (toques canônicos com
      pointer ID); editor em Play roteia os toques brutos ao input do
      jogo (§6.4); `eng::ui`/`eng::audio` são bibliotecas testadas à
      espera do consumidor de runtime (o runtime de jogo/NI-Script —
      limitação registrada na auditoria final)

## Estado — FASE 10 (concluída) — Physics + Animation + Particles

- [x] `eng::physics` (ADR-048): RigidBody (massa 0 = estático), Collider
      esfera/AABB com layers/masks/triggers, CharacterBody com
      move-and-slide + snapToGround (implementado na remediação da
      auditoria final), raycast estruturado, TIMESTEP FIXO por
      acumulador (determinismo testado), contatos expostos
- [x] `eng::animation`: clips TRS (lerp/SLERP), Animator componente
      (play/pause/stop natural/loop/speed/seek), transições com
      cross-fade linear APLICADO (o estado de blend vive no componente
      serializável e o `AnimationSystem` compõe as poses — correção
      C-13 da auditoria final); skeletal = extensão documentada
- [x] `eng::particles`: emitter CPU determinístico (spawn por
      acumulador, direção van der Corput sem RNG, burst, pool
      limitada); o viewport do editor DESENHA as partículas como quads
      (correção do drift D6 da auditoria final)
- [x] Integração: componentes refletidos + registrados no catálogo do
      serializer; em PLAY o tick avança física/animação/partículas sobre
      o CLONE (edição intacta)

## Estado — FASE 11 (concluída) — NI-Script

- [x] **Linguagem própria** (ADR-049): `.nis` → Lexer → Parser → Sema →
      Compiler → bytecode → NI VM — SEM Lua/Python/JIT; semântica de
      `repeat`/`repair`/`timeout` FORMALIZADA ANTES da implementação
      (`phase11_audit/design.md §5`)
- [x] `engine/niscript` (8 TU): 10 tipos + inferência, funções `f…stop`,
      eventos `up` + `emit` com propagação BFS por `link to`, módulos
      `add &BL`; diagnósticos com linha/coluna em toda etapa
- [x] **VM determinística**: orçamento global de instruções por evento
      (loop infinito IMPOSSÍVEL, sem threads), faults reparáveis
      (`repair`), orçamento por bloco (`timeout`), sem nil na linguagem
- [x] **Bindings refletidos** (ADR-043/D2): o editor registra a tabela
      reusando o catálogo + TypeRegistry — `e.position.x`,
      `e.rigidbody.velocity`, `comp(e,"RigidBody")`; geração respeitada
      (handle obsoleto = Fault, nunca UB)
- [x] **Integração PLAY** (ADR-044): `NiScriptComponent` no catálogo
      ÚNICO; play compila os scripts do CLONE e roda `@init`→`up start`
      → `up update` (por tick) →`up destroy`; script quebrado é
      desabilitado com log; edição nunca tocada
- [x] Docs: `docs/ni-script/` (8 arquivos) + `architecture/19` +
      ADR-049; UI de script no editor ADIADA e declarada

## Testes (estado pós-FASE 11)

**27 suites** — 100% verdes em `linux-debug` (ASan+UBSan+LSan, `-Werror`)
e `linux-release` (LTO), zero warnings; CI Linux executa os 27 com drivers
(lavapipe/EGL), CI Android monta e inspeciona o APK arm64-v8a. Contagens:
core 74 casos, rhi 16/410 (FakeBackend), vulkan 6, gles 5, android_runtime
10 (86 asserções com driver), **editor 35 casos/354 asserções** (inclui
play de scripts NI-Script), input 12/76, ui 8/40, audio 14/75, physics
15/59, animation 6/32, particles 6/33, **niscript 58 casos/516 asserções**
(semântica formal §5, E2E `.nis→ECS`, determinismo byte-a-byte,
segurança), + demais suites de fase. Auditoria 4–10 com classificação
[A]–[F], 18 bugs e remediação: `docs/final_phase4_10_audit.md`.

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

## Estrutura (FASE 11)

```
├── .devcontainer/         # camadas base/graphics/android + verify.sh
├── .github/workflows/     # ci-linux.yml + ci-android.yml
├── cmake/                 # EngineOptions/Warnings/Sanitizers/Dependencies
├── CMakeLists.txt         # raiz (C++20, módulos de política, tests/)
├── CMakePresets.json      # linux-debug | linux-release
├── docs/
│   ├── adr/               # ADR-021…048 (FASES 2–10)
│   ├── architecture/      # 00-overview + 02…18 por módulo
│   ├── phaseN_audit.md    # auditorias bloqueantes das fases
│   ├── final_phase4_10_audit.md  # auditoria final independente 4–10
│   ├── project-layout.md  # layout de projeto esperado
│   └── roadmap.md         # fases e estados
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
│   ├── rhi/               # Renderer/Frame + backends vulkan/ e gles/
│   ├── input/             # InputSystem canônico + ações (FASE 9)
│   ├── ui/                # widgets/draw-list/fonte 5×7 (FASE 9)
│   ├── audio/             # WAV/mixer/AAudio (FASE 9)
│   ├── physics/           # RigidBody/Collider/CharacterBody (FASE 10)
│   ├── animation/         # clips/animator/cross-fade (FASE 10)
│   ├── particles/         # emitter CPU determinístico (FASE 10)
│   └── niscript/          # NI-Script: lexer→sema→bytecode→VM+bindings (FASE 11)
├── editor/                # FASE 8: núcleo C++ do editor (consumidor)
├── android/               # FASES 7/8: runtime + editor Android (Gradle/APK)
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
Auditoria final independente das FASES 4–10 (bugs encontrados e
remediados): [docs/final_phase4_10_audit.md](docs/final_phase4_10_audit.md).

## Roadmap

| Fase | Escopo | Estado |
|---|---|---|
| 1 | core, math, mem, log + build/CI/devcontainer | ✅ concluída (`d9b2d9d`) |
| 2 | reflect, events, jobs, ecs, scene | ✅ concluída (`8469f7c`) |
| 3 | fs, platform, serial, assets, project + serialização de cena | ✅ concluída |
| 4 | renderer abstraction `eng::rhi` (ADR-035/036) | ✅ concluída |
| 5 | backend Vulkan real (ADR-037) | ✅ concluída |
| 6 | backend OpenGL ES real + paridade (ADR-038) | ✅ concluída |
| 7 | runtime Android: JNI, Activity, surface, APK arm64-v8a (ADRs 039–041) | ✅ concluída |
| 8 | Native Mobile Editor (ADRs 042–044) | ✅ concluída |
| 9 | eng::input + eng::ui + eng::audio (ADRs 045–047) | ✅ concluída |
| 10 | physics + animation + particles (ADR-048) | ✅ concluída |
| 11 | **NI-Script** — linguagem própria: lexer/parser/AST/sema/bytecode/VM/bindings ECS/debugger | planejada |
| 12 | **Build & Export Pipeline** — config/manifest/deps/cook/cache/validação/Android+Linux | planejada |

Detalhes por fase: [docs/roadmap.md](docs/roadmap.md).
