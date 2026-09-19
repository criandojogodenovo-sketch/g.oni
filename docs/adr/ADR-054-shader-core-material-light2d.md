# ADR-054 — Shader Core, uniforms de frame, Material System e Light2D (P3)

Data: 2026-09-19 · Estado: ACEITO · Bloco: P3 (2D RENDER CORE)

## Contexto

O bloco P2 fechou o authoring 2D, mas o render seguia no modelo da
ADR-042: **zero uniforms no RHI** — transforms CPU-baked por vértice,
shaders embutidos no editor, dois pipelines fixos (cor + sprite), nenhum
material, nenhuma luz. A própria ADR-042 definiu o gatilho da revisão:
"quando o RENDER DE JOGO existir" — o editor hoje renderiza o jogo (Play
renderiza o clone), e a iluminação 2D exige dados por fragmento que a
CPU não consegue prover (distância à luz com atenuação radial).

Requisitos do bloco (missão §2/§3/§5): Shader Core e Material REAIS
acima do RHI, Light2D como componente, tudo sem introduzir arquitetura
pesada — o alvo é Android low/mid-end (PowerVR/Mali/Adreno).

## Decisões

### D1 — Uniforms do frame no RHI (UBO por frame-slot)

`Frame::setUniformData(bytes)` + `RhiBackend::frameSetUniformData` (não
puro, default NotSupported — mesmo padrão do readCenterPixel):

- **Contrato**: dados COPIADOS no ato da chamada para uma REGIÃO própria
  do frame-slot em gravação (bump-alocada, alinhada 256B); valem para os
  draws seguintes até a próxima chamada; teto somado kMaxFrameUniformData
  (16KB por frame). Múltiplas chamadas = múltiplas regiões (ex.: conjunto
  de luzes por camada).
- **Vulkan**: UBO **dinâmico** (set 1/binding 0) — um buffer
  HOST_VISIBLE|HOST_COHERENT por frame-slot, mapeado persistentemente,
  descriptor escrito UMA vez; `vkCmdBindDescriptorSets` com dynamic offset
  seleciona a região. Escrita ANTES da submissão do frame, lida apenas
  por ele → **sem hazard entre frames in flight**, sem staging por
  update, sem fence por upload.
- **GLES**: `GL_UNIFORM_BUFFER` único (backend é sequencial — ADR-035) +
  `glBindBufferRange` por região (análogo do dynamic offset). O bloco é
  atribuído ao índice 0 via `ShaderDesc::uniformBlockName`
  (`glGetUniformBlockIndex` + `glUniformBlockBinding` no link — GLSL ES
  3.00 não tem qualifier binding).
- **Sampler NÃO mexeu**: set 0/binding 0 segue igual → todo o SPIR-V
  existente (triangle/sprite) permanece VÁLIDO sem regeneração.

Pipelines Vulkan agora declaram `[set0 sampler, set1 UBO]` — shaders sem
bloco apenas ignoram o set (legal e validado).

### D2 — engine/render (Shader Core)

Novo módulo ENTRE o editor e o RHI (editor → render → rhi → backends):

- `ShaderLibrary`: registra os shaders reais do 2D ("editor.color",
  "sprite.unlit", "sprite.lit") — MESMOS fixtures canônicos
  (tests/shaders, procedência FASES 5/6/P0-3) + par LIT novo
  (sprite_lit_{vk,gles}.{vert,frag}, glslangValidator 15.2.0
  --target-env vulkan1.1 -V --spirv-val). Fonte única dos layouts
  (32B/40B/48B). Nenhum tipo Vulkan/GLES vaza (o RHI absorve).
- `FrameParams` (`FrameUniforms`): bloco std140 "PerFrame" (288B:
  ambiente vec4 + lightA[8] + lightB[8] + meta) — layout BINÁRIO FIXO
  espelhado nos shaders, `static_assert` no C++ e teste de offsets.
- `RenderTypes` (`DrawList`): render world 2D genérica (câmera/ambiente/
  luzes/sprites/partículas) — o editor PREENCHE (EntityQuads → itens) e o
  ViewportRenderer CONSOME; extensível para 3D futuro (novos campos, não
  novos consumidores).

### D3 — Sprite lit: posição MUNDO por vértice

O fragment precisa da posição MUNDIAL do fragmento (luz vive em mundo) e
o batcher manda clip. Decisão: 4º attribute (location 3, vec2 worldXY —
48B/vertex) nos sprites LIT. O pipeline UNLIT mantém layout/shaders
anteriores (40B) — zero churn nos testes existentes.

### D4 — Material System (assets/materials)

`SpriteMaterial` {shader: "unlit"|"lit", tint RGBA} — TODOS os campos
consumidos de fato (shader escolhe o pipeline; tint multiplica o do
sprite; blend fixo alpha p/ quads 2D). Codec `.mat.json` (mesmas regras
do P2 de animações): shader inválido é ERRO no decode (não há fallback
silencioso). `EditorDocument` expõe CRUD + `resolveMaterials` (cache por
nome, invalidada em write/delete/troca de projeto; best-effort — material
ilegível → default lit neutro + log).

`SpriteData.materialAsset` (hint "material"): VAZIO = default "lit" com
tint neutro — **o look do editor não muda por osmose**.

### D5 — Light2D (componente real)

`eng::render::Light2D` {enabled, color RGB (grupo Inspector), intensity,
radius, falloff, layer} — posição vem do TRANSFORM da entidade (fonte
única). Registrado no catálogo único (ADR-043): reflexão/Inspector/
serialização/Play/clone de graça pelo caminho de RigidBody/Animator.

**Layer/mask REAL** (não campo decorativo): usa o LayerRegistry da Scene
(ADR-051). A luz ilumina apenas sprites cuja entidade é membro da MESMA
camada (`LayerMember.layer`; sem LayerMember = "GAME"). O renderer agrupa
runs por (shader, camada, textura) e re-sobe o bloco PerFrame quando a
camada muda.

### D6 — Modelo de iluminação (mobile-friendly)

Forward per-pixel NO fragment do sprite (sem render targets, sem passes
extras, sem MRT):

```
lighting = ambient.rgb * ambient.a
por luz: atten = clamp(1 - dist/radius) ^ falloff; lighting += cor * (intensidade * atten)
final = albedo * lighting   (albedo = textura * tint(sprite) * tint(material))
```

Ambiente default (1,1,1,1): **sem luzes = look clássico** (decisão: luz
ADICIONA sobre o ambiente — nunca escurece a cena por engano). Máx. 8
luzes por camada por frame (banco do bloco; excedente descartado com
honestidade — documentado).

## Consequências

- O sprite renderer passa PELO pipeline Material/Shader (missão §3): o
  shader do material seleciona o pipeline e o tint multiplica — nada
  decorativo.
- Runs preservam a ordem por sort (painter's algorithm ENTRE pipelines);
  binds por GRUPO (shader/camada/textura), nunca por sprite.
- Android: um único UBO HOST_VISIBLE por frame-slot (2 slots) + memcpy por
  mudança de camada — custo por frame desprezível; sem readback, sem
  alocação por frame.
- Limitações documentadas (ver docs/architecture/ e README do render):
  ambiente é global do frame (não por cena — entra com o render-graph);
  blend do sprite fixo alpha (material não troca blend ainda); máx. 8
  luzes/camada; edição do material via Assets (tint/alfa/shader) —
  edição inline no Inspector fica como evolução.
