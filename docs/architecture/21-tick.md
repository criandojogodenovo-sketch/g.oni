# 21 — eng::tick: Arquitetura de Tick (evolução P0-5)

> Fases canônicas do frame, agendador determinístico e ticks concretos.
> Decisões completas em [ADR-051](../adr/ADR-051-tick-architecture.md);
> camadas e links (mesma evolução) em [06-scene](06-scene.md).

## O problema (auditoria PHASE 0, §5.8)

Antes da P0-5 o avanço do frame era uma sequência FIXA escondida dentro de
`EditorDocument::tick` (input → física → animação → partículas → scripts).
Consumidores futuros (o runtime de bundles Android) não teriam como reusar
essa ordem; não existia ponto de extensão declarado. A P0-5 promove a ordem
do frame a ARQUITETURA: um agendador com fases, ordenação determinística e
ticks concretos que o editor e o runtime compartilham.

## Modelo: composição sobre o ECS

Um "tipo de Tick" no G.ONI é o PAR (componente que declara, sistema que
executa). `TickSystem` é uma INTERFACE (`name()`, `phase()`, `order()`,
`tick(scene, dt)`), não uma classe base de gameplay. Não existe registry
de kinds por string: um componente declarativo sem executor seria código
inerte (o que a missão proíbe). A taxonomia cresce adicionando pares
componente+sistema ao agendador.

| Fase | Responsabilidade | Ticks existentes |
|---|---|---|
| `PreUpdate` | estado do mundo assenta antes da lógica | `PhysicsTick` (timestep fixo) |
| `Update` | lógica de jogo | `AnimationTick` (order 10), `ParticleTick` (20), `ScriptTick` (30, no editor) |
| `PostUpdate` | pós-lógica (reservada) | — |
| `PreRender` | câmera ativa e ordenação visual | `CameraTickSystem` |

Ordenação determinística: `(fase, order() dentro da fase, ordem de
inserção)` — `std::stable_sort` preserva a inserção entre iguais.
`TickScheduler::addSystem` rejeita nome duplicado e sistema nulo.

## Ticks concretos

- **PhysicsTick** (PreUpdate): timestep fixo da FASE 10 — o dt do frame
  acumula, passos de tamanho fixo rodam a física. Corpos em camadas com
  `physics=false` são pulados dentro do próprio `PhysicsWorld`.
- **AnimationTick** (Update, 10): dt do frame escalado POR ENTIDADE pela
  camada (`Scene::timeScaleOf`).
- **ParticleTick** (Update, 20): idem animação — emissores em camadas sem
  `update` não nascem partículas.
- **ScriptTick** (Update, 30): vive no EDITOR porque depende de `NiRuntime`
  (camada de composição — mesmo padrão do catálogo de componentes,
  ADR-043). O `eng::tick` não conhece NI-Script.
- **CameraTickSystem** (PreRender): resolve/cacheia a câmera de jogo ativa
  e AVISA ambiguidade (>1 ativa — dados não ficam em silêncio).

## Câmera de jogo (CameraData)

Componente refletido (`posX`, `posY`, `zoom` em pixels-por-unidade — mesma
semântica da câmera do editor — e `active`). Ortográfica 2D;
rotação/perspectiva ficam para o rework de câmera 3D (P1): campo sem
consumidor seria API inerte.

`resolveActiveCamera(scene)` devolve a PRIMEIRA `CameraData` ativa em ordem
estável (índice de criação do pool). O resultado carrega os dados POR
VALOR: ponteiros de pool não sobrevivem a emplaces do mesmo tipo (o dense
array realoca — ADR-024), então `ActiveCamera` nunca retém ponteiro.

O viewport do editor, em Play com câmera ativa na cena, usa-a para TODAS as
conversões world↔screen (render, hit-test e arraste); gestos de pan/zoom
são ignorados nesse estado (a câmera é do jogo). Em Edit, ou em Play sem
câmera, a câmera do editor segue como antes.

## Uso (editor hoje, runtime de bundles depois)

```cpp
eng::tick::TickScheduler scheduler;
scheduler.addSystem(std::make_unique<eng::tick::PhysicsTick>(world, accumulator));
scheduler.addSystem(std::make_unique<eng::tick::AnimationTick>(bank));
scheduler.addSystem(std::make_unique<eng::tick::ParticleTick>());
scheduler.addSystem(std::make_unique<ScriptTick>(niRuntime));      // editor
scheduler.addSystem(std::make_unique<eng::tick::CameraTickSystem>());
// por frame:
scheduler.runFrame(scene, deltaSeconds);
```

`EditorDocument::tick` em Play é exatamente `input.update()` +
`scheduler.runFrame()`. `play()` NÃO roda frame nenhum: `up update` só
roda em tick explícito do host (contrato FASE 11 preservado).

## Extensões registradas (NÃO implementadas — não são bugs)

- Cache por frame da resolução de camadas (o custo hoje é lookup de
  componente + hash de string por entidade por frame — aceitável na escala
  do editor).
- Física per-layer timeScale (o timestep fixo é global; corpos em camada
  sem `physics` ficam estáticos).
- Integração de NI-Script com camadas (o `NiRuntime` itera instâncias,
  não entidades — registra para o P0-7).
- Índice de links por entidade (as consultas `eachFrom`/`eachTo` são
  O(n) sobre slots — dezenas/centenas de links não justificam).
- UI de links no editor (inspector/seletor) — trabalho do P0-6.
