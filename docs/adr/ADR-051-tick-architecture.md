# ADR-051 — Arquitetura de Tick: fases, camadas, links e câmera de jogo

- **Status**: ACEITO
- **Data**: 2026-09-18 (evolução P0-5)
- **Contexto**: `docs/goni_engine_audit_current.md` §5.8/§7-P0.5

## Contexto

A auditoria PHASE 0 apontou quatro lacunas arquiteturais herdadas das FASES
1–12:

1. **Não existe Tick/taxonomia de tipos** — o avanço do frame é uma
   sequência FIXA escondida dentro de `EditorDocument::tick`
   (input → física → animação → partículas → scripts). O runtime Android
   futuro (consumidor de bundles) não tem como reusar essa ordem; não há
   ponto de extensão declarado.
2. **Sem links entre entidades** — a única relação é parent/child da
   hierarquia. Não existe "A mira em B", "A segue B" como cidadão do
   modelo de objetos (a engine TS legada tinha o sistema Links com grafo,
   DFS de ciclos e propagação BFS).
3. **Sem camadas** — GAME/SUBGAME/nomeadas não existem; não há como
   pausar um grupo de entidades ou excluir um grupo da física/render sem
   desligar a cena inteira.
4. **Sem CameraTick** — a câmera é estado do EDITOR (`Viewport::Camera2D`),
   não da cena. Um jogo exportado não tem como definir SUA câmera.

## Decisão

### 1. Taxonomia de Ticks = composição sobre o ECS (sem árvore de herança)

**Um "tipo de Tick" no G.ONI é o par (componente que declara, sistema que
executa)**, agendado por um `TickScheduler` com fases canônicas:

| Fase | Responsabilidade | Ticks existentes |
|---|---|---|
| `PreUpdate` | estado do mundo assenta antes da lógica | `PhysicsTick` (timestep fixo) |
| `Update` | lógica de jogo | `AnimationTick`, `ParticleTick`, `ScriptTick` |
| `PostUpdate` | pós-lógica (reservada) | — |
| `PreRender` | câmera ativa e ordenação visual | `CameraTick` |

- `TickSystem` é uma INTERFACE (`name()`, `phase()`, `order()`,
  `tick(scene, dt)`), não uma classe base de gameplay. Sistemas são
  compostos no agendador — adicionar um tick novo não toca os existentes.
- Ordem determinística: `(fase, order() dentro da fase, ordem de
  inserção)`. `order()` devolve int (menor primeiro).
- `TickScheduler` vive em `eng::tick` (novo módulo). Os ticks de
  física/animação/partículas vivem LÁ (reusáveis pelo editor E pelo futuro
  runtime de bundles); `ScriptTick` fica no EDITOR (depende de
  `NiRuntime`, que é camada de composição — mesmo padrão do catálogo de
  componentes, ADR-043).
- **NÃO existe `TickDecl`/registry de kinds por string**: um componente
  declarativo sem executor seria código inerte (o que a missão proíbe). A
  taxonomia cresce adicionando pares componente+sistema.

### 2. Links tipados com handles estáveis — `eng::scene::LinkRegistry`

- Um link é um registro `(tipo, from, to)` em uma registry **de propriedade
  da `Scene`** (mesma política do `World`: ownership único).
- **Tipado**: tipos são registrados antecipadamente com flags
  (`hierarchical` — participa da detecção de ciclos). `create` com tipo
  não registrado é ERRO, não cria "link genérico".
- **Handles estáveis**: `LinkId{index, generation}` com free-list —
  destruir e criar não ressuscita handles antigos (mesma técnica de
  `eng::ecs::Entity`, ADR-024).
- **Detecção de ciclo**: ao criar um link hierárquico `from→to`, DFS/BFS
  verifica se `to` já alcança `from` por links hierárquicos — se sim,
  erro (ciclo). Links direcionais não participam do grafo de ciclos por
  design (um ciclo que só fecha por aresta direcional é permitido).
- Self-link (`from == to`) é proibido; `(tipo, from, to)` duplicado é
  proibido.
- **Propagação**: `eachReachable(tipo, from, fn)` — BFS transitivo sobre
  links daquele tipo (paridade com a engine TS legada).
- **Destruição**: `Scene::destroyNode` remove os links de TODAS as
  entidades da subárvore; bypass via `world().destroy()` deixa pontas
  mortas que são varridas por `sweep(world)` (chamado no save e nas
  consultas da Scene — tolerância oportunista, mesma política do
  ADR-025 para `Hierarchy`).
- **Serialização**: seção opcional `"links"` no JSON da cena
  (`{linkTypes: [...], entries: [{type, from, to}]}`) — tipos persistem
  no arquivo (nada de estado global); entidades por `SceneEntityId`;
  `from`/`to` ausente no load = ParseError; ordem de criação preservada
  (round-trip estável).

### 3. Camadas GAME/SUBGAME/nomeadas — `eng::scene::LayerRegistry`

- **Camadas são estrutura de cena** (não de runtime): a registry é de
  propriedade da `Scene` e persiste no arquivo. Isso permite que física,
  animação, partículas e viewport — que já recebem `Scene&` — consultem
  participação SEM nova dependência de módulo.
- Built-ins: `GAME` (default, tudo participante) e `SUBGAME` (segundo
  grupo de simulação independente — ex.: mundo do pause-menu/minigame).
  Nomeadas via `addLayer(name)`; remoção é REJEITADA enquanto qualquer
  entidade referencia a camada (varredura `each<LayerMember>` — sem
  fallback silencioso).
- **Participação** por camada: `update`, `physics`, `render` (bool).
- **timeScale** por camada (0 = pausada): animação e partículas escalam o
  dt POR ENTIDADE pela camada; física NÃO escala por camada em P0-5 (o
  timestep fixo é global — acumulador avança pelo relógio do frame;
  corpos em camada com `physics=false` saem do mundo físico: sem
  integração, sem resposta, sem trigger, sem raycast — um SUBGAME não
  bloqueia o GAME). Per-layer physics-time e matriz de colisão entre
  camadas são extensões registradas.
- Consulta canônica: `Scene::participatesIn(entity, LayerStage)` e
  `Scene::timeScaleOf(entity)` — ausência de `LayerMember` = GAME.
- `LayerMember` é COMPONENTE refletido (campo `layer`, default `"GAME"`)
  → aparece no Inspector, persiste, clona no Play.
- **Serialização**: seção `"layers"` (sempre emitida; defaults incluídos)
  — arquivos antigos (sem a seção) carregam com GAME/SUBGAME default.

### 4. CameraTick — a câmera vira cidadã da cena

- `eng::tick::CameraData` — componente (posX, posY, zoom em
  pixels-por-unidade — mesma semântica da câmera do editor — e flag
  `active`). Ortográfica 2D; rotação/perspectiva ficam para o rework 3D
  (P1) — campo sem consumidor seria API inerte.
- `resolveActiveCamera(scene)` — PRIMEIRA `CameraData` ativa em ordem
  estável (índice de criação). Determinístico; sem câmera → vazio.
- `CameraTickSystem` (fase `PreRender`) cacheia a câmera ativa por frame
  e AVISA quando há mais de uma ativa (ambiguidade é silêncio de dados).
- **Viewport do editor**: em Play, se a cena tem câmera ativa, TODAS as
  conversões world↔screen usam-na (render, hit-test e arraste seguem a
  câmera do jogo); gestos de pan/zoom são ignorados nesse estado (a
  câmera é do jogo — mexer nela por trás seria debug mentiroso). Em Edit,
  ou em Play sem câmera, a câmera do editor segue como hoje.

## Consequências

- `eng::tick` depende de `scene`, `physics`, `animation`, `particles`
  (ticks concretos) — torna-se o TOPO da pilha de gameplay; o grafo
  continua acíclico (`00-overview.md` atualizado).
- `EditorDocument::tick` em Play passa a ser `input.update()` +
  `scheduler.runFrame()` — mesma ordem de execução de antes (física →
  animação → partículas → scripts → câmera), agora DECLARADA e testável.
- Física/animação/partículas ganham checagens de participação por
  entidade (custo: lookup de componente + hash de string por entidade por
  frame — aceitável na escala do editor; cache por frame é extensão
  registrada em `21-tick.md`).
- Scripts NI-Script NÃO filtram por camada em P0-5 (o `NiRuntime` itera
  instâncias, não entidades; integração com camadas é registrada para o
  P0-7, quando a UI de script existir).
- UI de links no editor (inspector/seletor de entidade) é trabalho do
  P0-6 (inspector rework); os links são utilizáveis via API C++ e
  persistem.
- `formatVersion` da cena PERMANECE 1: as seções novas são aditivas e o
  parser ignora chaves de topo desconhecidas (compatível nos dois
  sentidos).
