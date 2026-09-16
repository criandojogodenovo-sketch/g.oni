# ADR-037 — Backend Vulkan: loader dinâmico, render pass clássico, staging

- **Estado:** aceito (FASE 5, missão §17–§29)
- **Contexto:** implementar Vulkan REAL (sem fake/mock/simulação) mantendo o
  build hermético (nenhum SDK obrigatório), CI verde em ambiente headless e
  caminho pronto para Android (FASE 7).

## Decisão

### Loader por dlopen (volk-like, ~55 funções)

`VulkanLoader` abre `libvulkan.so.1`/`libvulkan.so` via `dlopen` e resolve
`vkGetInstanceProcAddr`; funções globais/instance/device são carregadas por
níveis (`NULL` → instance → device, preferindo device-proc). Consequências:

- **zero link com libvulkan** — o target `eng_rhi_vulkan` só depende de
  headers e `${CMAKE_DL_LIBS}`;
- o mesmo binário roda em desktop e Android (a `libvulkan.so` do sistema é
  encontrada em runtime — missão §30);
- ausência do loader é ESTADO (`probe → Unavailable`), não erro de build
  (missão §11/§47).

### Headers via FetchContent com hash

Khronos/Vulkan-Headers **v1.4.309** (`URL_HASH` fixado) — mesmo padrão de
Catch2/nlohmann (ADR-030). Sem depender de `libvulkan-dev` local em
COMPILE (o devcontainer §12 pode ter, mas não é exigido).

### Render pass CLÁSSICO (dynamic rendering AVALIADO e adiado — missão §24)

`VkRenderPass` compartilhado (color-only, loadOp CLEAR) + framebuffers por
imagem da swapchain. Avaliação:

| Critério | Clássico | Dynamic rendering |
|---|---|---|
| Compatibilidade | 1.0+ (lavapipe, Android 1.1) | 1.3+/ext |
| Complexidade agora | render pass + framebuffers | begin sem framebuffers |
| Custo em resize | recriar framebuffers (feito) | nenhum |
| Quando importa | — | render-graph/MRT (futuro) |

Escolhido o clássico: universalmente compatível com os ambientes REAIS de
validação desta fase (lavapipe 1.4 em CI, requisitos mínimos 1.1). Dynamic
rendering entra junto com o render-graph/offscreen-target, quando houver
ganho mensurável — não habilitado às cegas.

### Viewport/scissor dinâmicos

Pipeline não depende do tamanho da surface: `resize()` não recria
pipelines. Default por frame = extent inteira (comandos `setViewport`
sobrepõem).

### Buffers: DEVICE_LOCAL + staging REAL

Vertex/index em `DEVICE_LOCAL`; `createBuffer(initialData)`/`updateBuffer`
fazem upload por buffer de staging HOST_VISIBLE|HOST_COHERENT + command
buffer dedicado + fence com espera (a fence garante visibilidade de memória
para submits futuros). `updateBuffer` é síncrono por contrato (documentado
— pode esperar). Allocator próprio minimalista (`pickMemoryType`);
**VMA rejeitado por ora** (missão §21: avaliar antes de adicionar — sem
necessidade real na escala atual; reavaliar quando FASE 7/8 escalar).

### Swapchain e apresentação

- Formato: preferido `B8G8R8A8_SRGB`, senão o primeiro reportado (REAL);
- Present: FIFO (garantido pela spec); MAILBOX se reportado;
- `imageCount = minImageCount + 1` (clamp ao max);
- `OUT_OF_DATE` no acquire → recria e tenta UMA vez; ainda fora de data →
  status `OutOfDate` (protocolo, não erro — auditoria F5/L3);
  `SUBOPTIMAL` → recria no próximo begin;
- `present()` drena TODOS os submetidos pendentes, em ordem;
- extent atual 0 → `Minimized`;
- **Surface: apenas Headless é criável nesta fase** (`VK_EXT_headless_surface`)
  — Xcb/Xlib/Wayland/Win32/Android ficam para as fases de plataforma
  (FASE 7/8), com os nomes de extensão já mapeados nos literais (sem
  `VK_USE_PLATFORM_*`, sem headers de plataforma — missão §14/§30).

### Frames in flight

`clamp(config.framesInFlight, 1, 2)`; por slot: command buffer, fence (criada
sinalizada), semáforos image-available/render-finished. Submissão canônica:
wait stage `COLOR_ATTACHMENT_OUTPUT`.

### Validação (missão §18)

`VK_LAYER_KHRONOS_validation` + `VK_EXT_debug_utils` quando pedida E
disponível; estados `Enabled/DisabledByConfiguration/Unavailable/
FailedToInitialize` reportados em capabilities — nunca mascarados. O log
chega pelo `eng::log` com categoria `rhi.vulkan.validation`.

### SPIR-V (missão §27)

Exigido (GLSL é do backend GLES — paridade §40). Validação de entry:
múltiplo de 4, ≥ 20 bytes, magic `0x07230203`; o loader + validation layers
fazem a validação completa. Estratégia de armazenamento: os testes embutem
SPIR-V GERADO de GLSL versionado em `tests/shaders/` (glslangValidator +
spirv-val; procedimento no README local) — hermético, sem dependência de
build-time.

### Concorrência e destruição

Single-threaded (ADR-035). `destroyBuffer/Shader/Pipeline` usam
`vkDeviceWaitIdle` — simples e correto nesta escala (registrado como
pendência de finer-grained sync quando houver sobrecução mensurável).

## Consequências e pendências registradas

1. **Readback de pixels** não implementado: o milestone §28 pede frame REAL
   submetido (cumprido e verificado com validation layers + contadores
   REAIS de submit/present); verificação por leitura de pixels entra com o
   offscreen-target futuro (não declarado como feito — missão §47).
2. **LSan × ICD**: lavapipe/layer Khronos retêm alocações internas até a
   descarga da biblioteca (ordem invisível ao LSan). Testes usam
   `tests/lavapipe_lsan.supp` (suppression POR MÓDULO — detecção de leaks
   do código do motor permanece ativa).
3. `wireframe` reportado `false` em capabilities: `fillModeNonSolid` não é
   habilitada no device nesta fase (feature opcional, sem consumidor).
4. Depth attachments ficam para o render-graph futuro (pipeline color-only;
   `DepthState` ativo sem attachment = erro preciso).
