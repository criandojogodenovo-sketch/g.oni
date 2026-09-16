# ADR-036 — rhi: seleção de backend, disponibilidade e validação

- **Estado:** aceito (FASE 4, missão §9/§10/§18/§47)
- **Contexto:** a missão exige seleção explícita (`Auto`/`Vulkan`/
  `OpenGLES`) SEM fallback silencioso, capabilities REAIS (nunca inventadas)
  e distinção honesta entre níveis de suporte — "libvulkan.so existe" não é
  "Vulkan funcionando".

## Decisão

### Registro de fábricas (executável final)

`Renderer::registerBackend(type, factory)` registra UMA fábrica por tipo
(`AlreadyExists` em duplicata). O registry é thread-safe (escrita exclusiva,
leitura compartilhada — política ADR-021/034) e `clearRegisteredBackends`
é API de teste documentada. Consequência: `eng_rhi` NÃO conhece backends —
o executável escolhe o que linkar (grafo alvo de 00-overview.md). Sem
registro: `create` falha com `NotFound` e mensagem acionável.

### Seleção (missão §10)

- **Auto**: ordem de preferência documentada `Vulkan → OpenGL ES`. Para cada
  tipo: UMA instância por tentativa — `probe()` (sem efeitos) e, se
  `availability >= Available`, `initialize()` NA MESMA INSTÂNCIA (probe não
  muta estado; instância descartada na rejeição). Motivos de rejeição são
  logados (`ENG_INFO`/`ENG_WARN`) e, se NADA estiver disponível, o erro
  final AGREGA TODOS os motivos (`NotSupported`).
- **Explícito**: sem registro → `NotFound` preciso; com registro →
  inicialização com validação COMPLETA; falha → erro preciso DO BACKEND,
  sem fallback. `allowFallback = true` habilita a continuação na ordem de
  preferência com `ENG_WARN` explícito ("fallback NUNCA silencioso" —
  missão §10).
- `Auto` + `allowFallback` é redundante (Auto já tenta todos) — logado
  como INFO, sem erro.

### Níveis de disponibilidade (missão §47)

`Availability`: `Unavailable < Detected < Available < Initialized < Capable
< Presentable < Rendering < Validated`. O probe reporta até onde der SEM
efeitos colaterais (tipicamente `Detected`/`Available`); níveis superiores
são alcançados em `initialize` e reportados por capabilities e pelos testes
de hardware das FASES 5/6 (que classificam `PASS/SKIPPED/UNAVAILABLE/
FAILED` via labels CTest + `SKIP()` do Catch2 com motivo).

### Validação honesta (missão §18)

`ValidationState {Enabled, DisabledByConfiguration, Unavailable,
FailedToInitialize}` em `RendererCapabilities`. O config expressa DESEJO
(`enableValidation`); a capabilities reportam REALIDADE. "Pedida e
inexistente" → `Unavailable` (nunca mascara como ativa). O FakeBackend
nunca finge validação (cenários explícitos).

### Capabilities reais (missão §9)

`RendererCapabilities`/`DeviceInfo` são preenchidos pelo backend
CONSULTANDO a API real (vkEnumerate*/glGet*) — o frontend não inventa
campo. `softwareRendering` é veracidade estrutural: suporte de software
(lavapipe/llvmpipe) nunca é reportado como hardware. `presentation`
distingue device-utilizável de surface-presentável (missão §13).

## Consequências

- A FASE 5 (Vulkan) valida a sonda/estado destes contratos contra a
  realidade (loader dinâmico, layers, GPUs físicas, queues); ajustes de
  granularidade viram revisão deste ADR.
- Erros sem código canônico (ex.: surface perdida) usam `StatusCode`
  existente + prefixo estável de mensagem — enum de domínio só entra se a
  FASE 5/6 demonstrar necessidade real.
