# G.oni Llumni — Arquitetura

## Visão geral

```
┌──────────────────────────────────────────────────────────┐
│                    EDITOR (editor/src)                    │
│  viewport · gizmos · painéis · visual scripting · play   │
└───────────────────────────┬──────────────────────────────┘
                            │ usa
┌───────────────────────────▼──────────────────────────────┐
│                   ENGINE (core/src/goni)                  │
│  Engine (orquestrador) · Scene · Objetos · Signal ·       │
│  Construt (prefabs) · Eliminação (pool/GC) · Links ·      │
│  Funciona · Formato .g.oni                                │
└──────┬────────────────────┬──────────────────┬────────────┘
       │                    │                  │
┌──────▼───────┐   ┌────────▼────────┐  ┌──────▼───────────┐
│   RENDER     │   │    FÍSICA       │  │  G.ONI SCRIPT VM │
│  WebGL2 PBR  │   │  impulsos+SAT   │  │  lexer→parser→   │
│  sombras HDR │   │  juntas raycast │  │  interpretador   │
│  pós/bloom   │   │  char. control  │  │  async (await)   │
└──────────────┘   └─────────────────┘  └──────────────────┘
```

## Módulos

### core/src/math
`Vec2/Vec3/Vec4`, `Quat`, `Mat4` (column-major), `Transform`, `Color`.
Matrizes seguem a convenção WebGL. Rotações Euler na ordem **Y-X-Z**
(yaw/pitch/roll). A inversa generalizada usa a fórmula clássica verificada.

### core/src/render
- **Renderer** — pipeline por frame:
  1. shadow pass (direcional, ortho ajustada aos bounds da cena, 2048²)
  2. cena em FBO **MSAA 4× + HDR (RGBA16F)**, com fallback automático
  3. céu procedural → opacos (frustum culling por esfera, frente→trás)
     → transparentes (trás→frente) → grid infinito (gl_FragDepth) → linhas
  4. resolve MSAA (blit) → bright pass (½ res) → blur gaussiano separável
     (2 iterações ping-pong) → composição final (bloom + ACES + exposição
     + vinheta + gamma)
- **PBR**: Cook-Torrance (D=GGX, G=Smith-Schlick, F=Schlick),
  workflow metal-roughness, ambiente hemisférico, até 4 pontuais + 2 spots,
  normal maps via *co-tangent frame* (sem atributos de tangente).
- **MeshRegistry** — compartilha `GpuMesh` entre objetos (menos draw calls).
- **LOD** — `MeshData.simplify()` por *vertex clustering* (grid), troca por
  distância.

### core/src/physics
- Integração semi-implícita de Euler com **passo fixo** (1/60) e substeps.
- Broadphase O(n²) com early-out por AABB (adequado a cenas ≤ ~200 corpos;
  hash espacial está no roadmap).
- Narrowphase: esfera-esfera, esfera-OBB (clamp em espaço local),
  **OBB-OBB por SAT de 15 eixos**, cápsula-cápsula (segmento-segmento).
- Solver: **impulsos sequenciais** (6 iterações) com restituição,
  atrito de Coulomb e correção posicional de Baumgarte.
- Inércia rotacional real (caixa/cápsula/esfera, diagonal local→mundo).
- Sleep com despertar por contato; eventos `on_collision_enter/exit`.
- Juntas por forças (fixed/spring/hinge/slider) — estáveis com damping.
- **CharacterController**: move & slide com substeps, step-height,
  ground check por raio.
- **Limitação v1 (documentada)**: cápsula-vs-caixa aproxima a cápsula por
  esfera central; física opera em espaço de mundo escrevendo no transform
  local de objetos-raiz.

### core/src/script (G.oni Script)
- **Lexer**: indentação (INDENT/DEDENT), `$No/Path`, comentários `#`.
- **Parser**: AST completa (funções, classes com herança, lambdas,
  dicionários, atribuições compostas).
- **Interpreter**: tree-walking **assíncrono** — toda avaliação retorna
  Promise, o que dá `await` nativo em qualquer função (corrotinas reais:
  `await wait(2)`, `await signal("hit")`). Orçamento de instruções por
  chamada evita loops infinitos travarem a aba.
- **StdLib**: ~50 funções globais + **bridge de objetos** que expõe
  GOniObject/CharacterController como cidadãos de primeira classe
  (`obj.position.x = 2` escreve direto no transform, por referência viva).
- Árvores de sintaxe em cache por script (hot-reload limpa o cache).

### core/src/goni (sistemas G.oni)
Cada sistema é uma classe isolada com API pública estável:

| Sistema | Arquivo | Papel |
|---|---|---|
| G.oni Signal | `Signal.ts` | emit/connect/disconnect + `waitSignal` (para `await`) |
| G.oni Objetos | `Objetos.ts` | entidades + 7 componentes + hierarquia |
| G.oni Links | `Links.ts` | grafo de arestas, DFS de ciclos, propagação BFS limitada |
| G.oni Funciona | `Funciona.ts` | registro de funções nativas/script + hot-reload |
| G.oni Construt | `Construt.ts` | prefabs, spawn, snap, group |
| G.oni Eliminação | `Eliminacao.ts` | destroy imediato/diferido, pooling, GC, log |
| Engine | `Engine.ts` | ciclo de vida play/stop (snapshot+restore), update loop, coleta de render |

### Editor (editor/src)
UI **vanilla TS + CSS custom** (sem frameworks — bundle mínimo, toque fluido).
- **Viewport**: orbit/pan/zoom por toque e mouse, WASD fly (desktop),
  picking por raio→esfera→**triângulo (Möller–Trumbore)**, F foca.
- **Gizmo**: eixos com hit-test em espaço de tela; mover usa
  ponto mais próximo **raio↔reta 3D** (matematicamente exato);
  rotacionar por ângulo de tela; escalar por razão de distância.
- Painéis: hierarquia, inspetor, scripts (+console), visual scripting
  (canvas 2D com pan/pinça), modelagem, animação (timeline com
  scrubbing e preview), texturização (pintura raycast→UV→canvas→GPU),
  assets/mundo.
- **Undo/redo** por snapshots (30 níveis). **Autosave** 45s. beforeunload.
- **Play mode**: snapshot da cena → instanciar scripts → física →
  joystick virtual + teclado → restore ao parar.

### Persistência
IndexedDB (stores `projects` + `files`). O **VFS** espelha a estrutura
`scenes/ scripts/ models/ textures/ animations/ + project.json`.
O arquivo `.g.oni` (export/import) é o mesmo conteúdo empacotado
(JSON + gzip via CompressionStream, com cabeçalho mágico `GON1`).

## Migração futura para C++/WASM

`core/src/math` e `core/src/physics` não tocam em DOM. O port consiste em:
1. Compilar com Emscripten mantendo classes e assinaturas
2. Carregar o `.wasm` em `Engine.init()`
3. Bindings mínimos nos pontos de fronteira (Engine→render/script)

Nenhuma outra camada muda. Ver `core/src/*` para as interfaces exatas.

## Limitações conhecidas (v1)

- Occlusion culling: frustum culling implementado; oclusão por portais/occluders está no roadmap
- Edição de modelagem é por malha inteira (subdivide/extrude/bevel/noise/smooth); seleção por vértice/face é roadmap
- Hinge/slider joints são aproximações por molas direcionais
- Física de cápsula vs caixa usa aproximação esférica
- Máx. 1 luz direcional com sombra + 4 pontuais + 2 spots por frame
