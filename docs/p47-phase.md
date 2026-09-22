# P4.7.0 — Contrato de Componentes · Eventos · Câmera · Física Varrida · Performance

HEAD base: `6c2c454` (P4.6 fechado em `cbacb72` + evidência CI). Fase em 6
blocos ordenados (bloco seguinte só com o anterior verde). Este documento é
a adenda de evidência por bloco. Feedback driver: round 6 (gizmo "cubo",
UI sobreposta, colisão sem resposta para script ingênuo, sem câmera, sem
culling/governor) + auditoria de arquitetura (sem contrato de componente,
sem hooks, sem eventos tipados).

## Bloco 0 — Versão visível

- `versionName "0.7.0"` / `versionCode 70` em `android/app/build.gradle.kts`.
- BuildConfig: `PHASE_LABEL "P4.7.0"`, `BUILD_NAME "0.7.0"`, `BUILD_CODE 70`,
  `GONI_COMMIT` (injetado pelo CI; builds locais vazios — opcional por design).
- Splash: caption `P4.7.0` regenerada por `scripts/gen_splash_caption.py P4.7.0`
  (o texto DINÂMICO real vive em BuildConfig; PNG é asset, não fonte).
- Sheet Configurações → "Versão": `P4.7.0 · build 0.7.0 (70) · <hash>` —
  100% BuildConfig (EditorDialogs.kt), zero hardcode.

| Item | Status |
|---|---|
| versionName/Code/PHASE_LABEL via BuildConfig | VERIFIED (código; APK no fecho) |
| caption no splash drawable | VERIFIED (asset gerado; visual no round 7) |
| linha Versão na sheet | VERIFIED (código; visual no round 7) |

## Bloco 1 — ComponentContract v2 + hooks + eventos tipados

### Contrato (`eng::scene::detail::ComponentContract`)

- `{required, conflicts, single, category, scriptAlias}` registrado NO
  catálogo único do serializer (ADR-043 — uma fonte só para Inspector,
  NI-Script e validação). `requires` do contrato chama-se `required` em
  código (palavra-chave C++20); a semântica é a mesma.
- **Add com erro preciso** (`Inspector::addComponent`):
  - `required` ausente → `"X exige Y — adicione Y antes"`
  - `conflicts` presente → `"X conflita com Y na mesma entidade — remova Y primeiro"`
  - `single` com instância viva → `"X é único na cena (single)"` (via
    `ComponentEntry::count` — novo ponteiro `count(world)` type-erased)
  - Add é ATÔMICO: contrato violado ⇒ nada é anexado.
- **Remove com dependência reversa** (`Inspector::removeComponent`):
  outro componente presente que `required` o removido ⇒ recusa com o
  NOME do dependente (`"não é possível remover Collider: CharacterBody
  exige este componente — remova CharacterBody primeiro"`).
- Contratos são APLICAÇÃO DE AUTORIA; o LOAD não re-injeta dependências
  (cenas salvas já satisfazem; hand-edits abrem, nunca falham por contrato).

### Contratos nativos registrados (ComponentRegistration.cpp)

| Componente | Categoria | Apelido NI-Script | requires/conflicts/single |
|---|---|---|---|
| eng::math::Transform | Transform | — | — |
| eng::scene::Name | Transform | — | — |
| eng::scene::LayerMember | Lógica | layer | — |
| eng::editor::SpriteData | Render | sprite | — |
| eng::physics::RigidBody | Física | rigidbody | conflicts CharacterBody |
| eng::physics::Collider | Física | collider | — |
| eng::physics::CharacterBody | Física | character | requires Collider; conflicts RigidBody |
| eng::animation::Animator | Lógica | animator | — |
| eng::particles::ParticleEmitter | FX | particles | — |
| eng::editor::NiScriptComponent | Lógica | script | — |
| eng::tick::CameraData | Câmera | camera | — (múltiplas permitidas) |
| eng::editor::AudioSource | Áudio | audio | — |
| eng::render::Light2D | FX | light | — |

- Ordem fixa de categorias: Transform → Render → Física → Lógica → Áudio →
  Câmera → FX → Outros (`Inspector::catalogEntries()`; JNI
  `nativeEditorComponentCategories` → dialog "Adicionar componente"
  AGRUPADO por categoria com cabeçalhos não-clicáveis; busca ativa = lista
  plana). Apelidos: contrato primeiro; apelido legado (cauda minúscula do
  nome) CONTINUA registrado — scripts antigos nunca quebram.
- Hints de dependência do dialog vêm do CONTRATO primeiro
  (`exige eng::physics::Collider`), legacy depois.

### Hooks (`onAttach` / `onDetach` / `onValidate`)

- Registrados junto do tipo (`registerComponentType` com contrato+hooks, ou
  `registerComponentContract` p/ built-ins). `hookUser` = contexto do
  chamador (o documento passa `this`).
- **Registro NATIVO de efeitos colaterais vive SÓ nos hooks** — o caso
  especial "Light2D casa com a camada dos sprites lit" MIGROU do
  EditorDocument para `light2DAttach` (mesma semântica do P4.6 B2:
  contagem por LayerMember, material vazio = lit, cache frio = lit,
  vencedor = maior contagem; novo acesso `EditorDocument::
  materialCountsAsLit`). Testes P4.6 continuam verdes.
- **onValidate** (`Collider`): radius/halfExtents negativos recusados no
  `setField` com ROLLBACK pelo valor anterior (round-trip string neutro) —
  o componente nunca fica quebrado. No add: default inválido ⇒ rollback do
  anexo. No **play()**: validação completa da cena ANTES do clone
  (`"não é possível entrar em Play: X no nó N é inválido — motivo"`) —
  cenas corrompidas por caminho externo não entram em jogo.
- onDetach: roda pós-remoção bem-sucedida (nenhum nativo precisa em B1;
  mecanismo + testes prontos).

### Eventos tipados (`Scene::events()` + `eng::scene::SceneEvents.hpp`)

- `eng::events::EventBus` (ADR-022) agora VIVE na cena
  (`scene.events()`; scene → events PUBLIC).
- `HitEvent{self,other,nx,ny}` — publicado pela física a cada contato
  resolvido, NOS DOIS SENTIDOS (self/other trocados; normal aponta de
  other para self). `TriggerEvent{self,other,entered}` — diff de pares de
  trigger entre passos (`on_enter` uma vez, `on_exit` ao separar; pares
  canônicos, determinístico). `VisibilityEvent{entity,visible}` — evento
  definido; o PUBLISHER é o culling do Bloco 6.
- **Bridge NI-Script** (`NiRuntime`): inscreve no barramento no start;
  `up on_hit:` / `up on_enter:` / `up on_exit:` / `up on_visible:` /
  `up on_invisible:` rodam nas instâncias cujo `self` == entidade do
  evento (inline durante o publish — ordem determinística da física;
  guarda anti-reentrância). Faults contam em `NiScriptStats` como update.
  Inscrições são RAII e morrem no shutdown (nunca sobrevivem à cena de Play).

### JNI/Kotlin

- Novo: `nativeEditorComponentCategories(handle)` — TSV
  `name\tcategory\talias` (contrato JNI por dlsym atualizado: 130 símbolos).
- Dialog "Adicionar componente" agrupado por categoria (cabeçalhos em
  acento, não-clicáveis; índice de clique vem da lista paralela
  `selectable` — nunca da lista exibida).

### Legado ajustado (comportamento intencional)

- `p46: REPRO` (CharacterBody): ordem de autoria atualizada para
  Collider ANTES de CharacterBody — o contrato ensina a ordem certa
  (antes o add silenciosamente aceitava e `move_and_slide` não tinha
  esfera pra varrer).

| Item | Status |
|---|---|
| requires/conflicts/single com erro preciso + add atômico | VERIFIED (5 testes) |
| Remove com dependente nomeia quem exige | VERIFIED |
| Categorias em ordem fixa + catalogEntries | VERIFIED (2 testes) |
| onValidate rollback + play() recusa cena inválida | VERIFIED (2 testes) |
| Hook da luz (mesma semântica P4.6) | VERIFIED (testes P4.6 + novo) |
| HitEvent dois sentidos / trigger enter-exit | VERIFIED (3 testes física) |
| Bridge on_hit roda no self atingido | VERIFIED (e2e doc+script) |
| Bus da cena publish/subscribe/RAII | VERIFIED |
| Dialog agrupado por categoria | VERIFIED (código; visual no round 7) |
| on_visible/on_invisible (publisher no B6) | PARCIAL (bridge pronto) |

## Bloco 2 — Gizmos v3: setas REAIS, hit ≥48dp, halo, anti-sobreposição

Driver: round 6 — "o gizmo parece um cubo, não setas" (as pontas eram
quadrados girados) e handles colapsando sobre o centro em bounds pequenos.

- **Setas reais** (`GizmoTriangle` — novo primitivo do gizmo; renderer
  emite 3 vértices): MOVE = 4 pontas triangulares apontando PARA FORA
  (16dp de comprimento, 14dp de base, halo incluído), hastes 2dp
  terminando na BASE do triângulo (nunca através). Rotate = anel + handle
  TRIANGULAR tangente (aponta na direção de crescimento do ângulo; o
  chevron de segmentos saiu — o triângulo É a seta). Scale = 4 cantos
  (quadrados, escala XY) + 4 marcas de aresta + 4 setas triangulares de
  aresta apontando para fora ao longo do eixo local.
- **Centro DIAMANTE** no MOVE (quadrado a 45°) — affordance distinta dos
  cantos quadrados do SCALE.
- **Halo 1dp** sob TODA forma do gizmo (quads já tinham rim; agora
  segmentos recebem linha bg mais grossa por baixo e triângulos halo
  próprio) — contraste garantido sobre sprites claros.
- **Anti-sobreposição clamp** (`scaleHandlePoints`, fonte ÚNICA de
  hit-test e desenho): bounds pequenos empurram cantos para fora até
  52dp do centro e arestas até 44dp — o cluster de cantos nunca mais
  colapsa num "cubo" sobre a entidade. Drag continua 1:1 screen-space
  (segue o POINTER, não o handle clampado).
- **Hit ≥48dp mantido** (kHitDp 24 → ⌀48dp) e agora SEMPRE coincidente
  com o visual desenhado (mesma `scaleHandlePoints`).
- **Camada de desenho**: grid → sprites (+borda de seleção) → gizmo
  (quads → segmentos → triângulos) → HUD do Play — inalterada na ordem
  macro, triângulos por cima das hastes dentro do lote.

| Item | Status |
|---|---|
| Setas triangulares reais (move/scale/rotate) | VERIFIED (3 testes de layout) |
| Centro diamante | VERIFIED |
| Hastes terminam na base do triângulo | VERIFIED |
| Clamp anti-sobreposição (hit == visual) | VERIFIED (2 testes) |
| Halo 1dp (quads+segmentos+triângulos) | VERIFIED (código; visual no round 7) |
| Drag 1:1 preservado (regressões P1/P2 verdes) | VERIFIED (suite completa 3×) |

## Bloco 3 — zero sobreposição (top bar / auditoria)

(este bloco ainda não começou)
