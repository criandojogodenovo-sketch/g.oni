# Arquitetura — Visão Geral (FASE 2)

> Documento normativo do grafo de dependências entre módulos (PARTE 2 do
> contrato). **Regra:** dependências unidirecionais, sem ciclos; nenhum módulo
> `engine/*` inclui headers de `android/`, `editor/` ou de backends de outros
> módulos.

## Estado atual (FASE 1 + FASE 2 — entregues)

A FASE 1 entrega quatro módulos fundacionais **independentes** (grafo
acíclico por construção). A FASE 2 entrega cinco módulos sobre a fundação —
`reflect`, `events`, `jobs` declaram a camada sobre `core` (sem consumir
símbolos ainda); `ecs` sobre `core`/`reflect` (idem, §B.4); `scene` consome
`ecs` e `math` de fato:

```mermaid
graph TD
    subgraph "FASE 1 (entregue)"
        core["eng::core<br/>Result, Error, Span, Version"]
        math["eng::math<br/>Vec2/3/4, Mat4, Quat, Transform"]
        mem["eng::mem<br/>Allocator, Heap, Arena"]
        log["eng::log<br/>Logger, Sinks, Format, Macros"]
    end
    subgraph "FASE 2 (entregue)"
        reflect["eng::reflect<br/>TypeRegistry, ENG_REFLECT (ADR-021)"]
        events["eng::events<br/>EventBus, Subscription (ADR-022)"]
        jobs["eng::jobs<br/>JobSystem work-stealing (ADR-023)"]
        ecs["eng::ecs<br/>World sparse-set (ADR-024)"]
        scene["eng::scene<br/>hierarquia + transforms (ADR-025)"]
    end
    core -.->|camada declarada| reflect
    core -.->|camada declarada| events
    core -.->|camada declarada| jobs
    core -.->|camada declarada| ecs
    reflect -.->|aresta declarada, §B.4| ecs
    math --> scene
    ecs --> scene
```

**Matriz de adjacência (entregue):**

| Módulo | Depende de | ADR |
|---|---|---|
| `eng::core` | — | — |
| `eng::math` | — | — |
| `eng::mem` | — | — |
| `eng::log` | — | — |
| `eng::reflect` | core (declarada) | [021](../adr/ADR-021-reflection-strategy.md) |
| `eng::events` | core (declarada) | [022](../adr/ADR-022-events-lifetime.md) |
| `eng::jobs` | core (declarada) | [023](../adr/ADR-023-jobs-architecture.md) |
| `eng::ecs` | core, reflect (declaradas) | [024](../adr/ADR-024-ecs-storage.md) |
| `eng::scene` | core (declarada), **ecs, math (consumidas)** | [025](../adr/ADR-025-scene-hierarchy.md) |
| executáveis de teste | módulo testado + Catch2 (externa) | — |

Documentação por módulo: [02-reflect](02-reflect.md) ·
[03-events](03-events.md) · [04-jobs](04-jobs.md) · [05-ecs](05-ecs.md) ·
[06-scene](06-scene.md).

Decisão registrada: `eng::mem` reporta vazamentos via `fprintf(stderr)` no
destrutor do `HeapAllocator` em vez de usar `eng::log`. Motivo: introduzir a
aresta `mem → log` criaria um precedente de camada invertida (log é consumidor
de memória, não o contrário) e um risco de ciclo quando `eng::log` adotar
alocadores de `eng::mem` em fases futuras. A API expõe `hasLeaks()`/`stats()`
para que o runtime reporte via logger quando existir.

## Módulos planejados (grafo alvo, por fase)

Arestas futuras — **referência de projeto, não código existente**:

```mermaid
graph TD
    core --> fs["eng::fs (FASE 3)"]
    core --> assets["eng::assets (FASE 3)"]
    math --> rhi["eng::rhi (FASE 4)"]
    platform["eng::platform (FASE 3)"] --> rhi
    rhi --> rhi_vulkan["rhi-vulkan (FASE 4)"]
    rhi --> rhi_gl["rhi-gl (FASE 5)"]
    physics["eng::physics (FASE 6)"] --> physics_jolt["physics-jolt (FASE 6)"]
    audio["eng::audio (FASE 6)"] --> audio_ma["audio-miniaudio (FASE 6)"]
    script["eng::script (FASE 7)"] --> script_lua["script-lua (FASE 7)"]
    runtime["eng::runtime (FASE 8)"] --> core & math & scene & rhi & physics & audio & script
```

Regras que mantêm o grafo acíclico conforme os módulos entram:

1. Módulos de **interface** (`eng::rhi`, `eng::physics`, ...) nunca incluem
   headers de **backends** (`rhi-vulkan`, `physics-jolt`, ...). Backends
   implementam interfaces e são linkados no executável final.
2. Fundações (`core`, `math`, `mem`, `log`) nunca ganham dependências para
   módulos de nível superior.
3. `android/`, `editor/` são consumidores de `engine/` — nunca o contrário.

## Isenções de política de compilação (registradas)

- **ADR-004 (sem exceções):** válido para as bibliotecas `eng::*`
  (`-fno-exceptions`). Executáveis de **teste** mantêm exceções habilitadas
  porque Catch2 as exige para reportar falhas.
- **ADR-005 (sem RTTI):** válido para bibliotecas `eng::*` **e** executáveis de
  teste (`-fno-rtti` em ambos — `eng_apply_test_policy`). Motivo técnico: com
  RTTI habilitado nos testes, o GCC emite referências a `typeinfo` de classes
  polimórficas do motor que os TUs `-fno-rtti` nunca definem, quebrando o link.
  Catch2 v3.5.2 compila limpo sem RTTI nesta configuração.
- **C++20 modules:** desativados (`CMAKE_CXX_SCAN_FOR_MODULES OFF`) — a
  varredura automática do CMake injeta `-fmodules-ts`, que conflita com a
  combinação lib(`-fno-rtti`) + testes ao linkar. Reavaliar quando módulos
  forem adotados de fato.

## Layout de módulo (padrão para todos os `engine/*`)

```
engine/<módulo>/
├── CMakeLists.txt        # eng_add_module(<módulo> ...) + testes
├── include/eng/<módulo>/ # headers públicos
├── src/                  # implementação (.cpp)
└── tests/                # testes Catch2 v3
```
