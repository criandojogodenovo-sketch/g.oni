# ADR-035 — rhi: contrato da abstraction e do backend

- **Estado:** aceito (FASE 4, missão §4/§6/§7/§8/§11/§12/§13/§14)
- **Contexto:** as FASES 5/6 implementam Vulkan e OpenGL ES REAIS. Para que
  scene/ecs/assets/jobs jamais dependam dessas APIs, o motor precisa de uma
  interface de hardware gráfico que represente INTENÇÃO de alto nível. O grafo
  alvo (docs/architecture/00-overview.md) já reservava `eng::rhi` com backends
  linkados no executável final.

## Decisão

### Módulo e fronteira

- `engine/rhi` (namespace `eng::rhi`, target `eng_rhi`) — dependências
  EXATAS: `eng::core` (PUBLIC) + `eng::log` (PRIVATE). Zero dependência
  gráfica em compile/link (verificável por `target_link_libraries`, includes
  e `compile_commands.json`).
- Backends vivem em `engine/rhi/backends/{vulkan,gles}` (FASES 5/6), atrás
  das opções `ENG_BUILD_RHI_VULKAN`/`ENG_BUILD_RHI_GLES` — NENHUM diretório
  criado na FASE 4. Nenhum módulo engine/* inclui headers de backend.
- API pública de alto nível (missão §4): `Renderer::createBuffer/...`,
  `beginFrame/endFrame/present` — sem `createVkInstance`, sem tipos `Vk*`,
  `GLuint`, `EGL*` em qualquer header de rhi.

### Contrato `RhiBackend` — pequeno e coeso (missão §7)

Uma única interface (~25 operações), coesão por ciclo de vida:
probe/initialize; recursos (buffer/shader/pipeline + update/destroy); frame
(frameId + frameClear/SetViewport/SetPipeline/Bind*/Draw*/endFrame/present);
surface (resize/surfaceLost). Justificativa contra a alternativa de
sub-interfaces (device/factory/commands/present): TODAS as operações fazem
sentido para QUALQUER backend real — separar adicionaria vtables sem
eliminar nenhum obrigatório. A missão probe "não obrigar operações sem
sentido" — nenhuma operação do contrato é sem sentido para Vulkan/GLES.

### Handles opacos tipados (missão §8)

`Handle<Tag>` com `std::uint64_t id` (0 = nulo). Codificação interna
(índice+geração nos 32 bits altos) é detalhe do BACKEND — o contrato do
frontend é: nulo é inválido; desconhecido/stale retorna `InvalidArgument`
preciso; nunca UB; nunca ponteiro. `BufferHandle`/`ShaderHandle`/
`GraphicsPipelineHandle` são tipos distintos por tag — troca acidental é
erro de compilação.

### Ownership/lifetime

- `Renderer` e `Frame` são **move-only, RAII**. Moved-from é defensivo:
  operações retornam `InvalidArgument`, não undefined behavior.
- Recursos são owned pelo backend; destruição explícita ou em cascata no
  dtor do backend (o dtor do `Renderer` libera tudo).
- `Frame` vivo mantém o backend vivo via **estado compartilhado**
  (`detail::RendererState` em `shared_ptr`): destruir o `Renderer` durante a
  gravação é SEGURO — o `Frame` termina no próprio dtor (end fail-safe,
  logado em `Warn`) e o backend é liberado depois. Custo: um control block
  por `Renderer`; benefício: impossibilita dangling por construção.
- Uma sessão de gravação por vez: `beginFrame` com frame em gravação é
  erro. Frame SUBMETIDO (end sem present) não bloqueia o próximo begin
  (espelha frames-in-flight reais).

### Frame lifecycle (missão §12)

`beginFrame → comandos → end → present`. Estados de protocolo NÃO são
erro: `AcquiredFrame.status ∈ {Renderable, OutOfDate, Minimized}`; o
`OutOfDate` absorve recriação de swapchain no backend (contabilizada,
logada) e `Minimized` pula o tick. `SurfaceLost`/sem-surface são erros
precisos (`NotSupported` + mensagem "rhi.surface: ..."). `present` sem
frame submetido → `NotSupported`.

### Device-only × presentation (missão §13)

`RendererConfig::surface` ausente → modo device-only: recursos e
capabilities funcionam; `beginFrame`/`resize`/`present` retornam erro
preciso. A ausência de surface NÃO significa backend indisponível — é um
ESTADO diferente, refletido em `RendererCapabilities::presentation`.

### FakeBackend (missão §11)

Determinístico, vive EXCLUSIVAMENTE em `engine/rhi/tests/` (nunca na
biblioteca, nunca produção, nunca satisfaz testes gráficos reais).
Capabilities marcadas: `backendName = "Fake (TESTES)"`,
`softwareRendering = true`. Testa: lifecycle, handles/stale (gerações),
erros, protocolo de frame, ownership (teardown reporta recursos
pendentes), seleção/fallback, validação honesta.

## Consequências

- `Result<T>` com T move-only (`Renderer`, `AcquiredFrame`) validado:
  Result nunca instancia cópia de T; testes controlam fluxo.
- Texturas/samplers/compute/async ficam FORA (missão §6) — pontos de
  extensão documentados; `assets::loadAsync` não entra nesta fase.
- A granularidade de `StatusCode` é limitada por projeto (core): surface
  perdida mapeia para `NotSupported` com prefixo de mensagem estável
  `rhi.surface: perdida` — se a FASE 5/6 precisar de mais granularidade,
  enum de domínio rhi entra em ADR própria (ver ADR-036 §Seleção).
