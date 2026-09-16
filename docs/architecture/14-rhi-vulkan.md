# eng::rhi::vulkan — Backend Vulkan real

- **Fase:** 5 (missão §17–§29) · ADR-037 · auditoria `docs/phase5_audit.md`
- **Target:** `eng_rhi_vulkan` (`engine/rhi/backends/vulkan/`) — linkado no
  executável final; `eng::rhi` não conhece este diretório
- **Dependências:** `eng::rhi` (implementa `RhiBackend`), Vulkan-Headers
  v1.4.309 (FetchContent, hash fixado), `dl` (loader em runtime)

## O que é REAL aqui

Nenhum fake/mock/simulação (missão §11):

| Requisito da missão | Implementação |
|---|---|
| Instance real (§17) | `vkCreateInstance` via loader `dlopen`; versão consultada |
| Validation layers (§18) | `VK_LAYER_KHRONOS_validation` + debug utils, estados honestos |
| GPUs reais (§19) | TODAS enumeradas; requisitos (graphics/present/swapchain); motivo por rejeição |
| Device/queues (§20) | `vkCreateDevice`; graphics + present |
| Commands/sync (§22) | pools, command buffers, fences, semáforos, 2 frames in flight |
| Surface/swapchain (§23) | `VK_EXT_headless_surface` → capabilities/formatos/present modes reais; recriação em out-of-date/suboptimal; minimized |
| Pipeline (§24) | `VkRenderPass` clássico (ADR-037), viewport/scissor dinâmicos, vertex input do `VertexLayout`, raster/blend da intenção |
| Buffers (§25) | vertex/index `DEVICE_LOCAL` + upload por staging REAL (fence com espera) |
| Shaders (§27) | SPIR-V validado (magic/tamanho) — `createShader` exige as duas stages |
| Milestone (§28) | teste `triangle REAL submetido à GPU`: begin→clear→pipeline→vbo→draw(3)→end(submit)→present — com layers ativas |
| Android (§30) | sem JNI; loader `libvulkan.so` por dlopen; `NativeWindowKind::Android` mapeado (`VK_KHR_android_surface`) mas não criável até a FASE 7 |

## Como rodar os hardware tests

```bash
# Linux ( loader + ICD/lavapipe + layers de validação )
export VK_ICD_FILENAMES=.../lvp_icd.json          # ICD software (opcional)
export VK_LAYER_PATH=.../explicit_layer.d          # layers (opcional)
export LD_LIBRARY_PATH=...                         # se libs fora do sistema
ctest --preset linux-debug -L rhi_hardware --output-on-failure
```

Sem loader/ICD o teste **SKIPA com motivo** — nunca falha por ausência de
GPU (missão §29/§41). CI instala `libvulkan1 mesa-vulkan-drivers
vulkan-validationlayers` (ICD/layers nos paths padrão, sem env).

## Estados e honestidade

- `probe()`: `Unavailable` (loader ausente) ou `Detected` (loader +
  versão) — níveis acima exigem `initialize` (ADR-036/L2);
- capabilities 100% consultadas: deviceName/apiVersion/limits/formatos
  verificados via `vkGetPhysicalDeviceFormatProperties`;
- `softwareRendering = true` quando llvmpipe/SwiftShader/CPU — suporte de
  software NUNCA é reportado como hardware (missão §47);
- contadores REAIS em `VulkanStats` (framesSubmitted/presentsOk/
  swapchainRecreations/gpusEnumerated/gpusRejected/validationLayerActive).

## Limitações registradas (não implementado — não declarado)

Readback de pixels (pendente de offscreen-target), depth attachments
(render-graph futuro), MRT, compute, texturas na abstraction (adiadas),
wireframe (`fillModeNonSolid` não habilitada), desktop surfaces
(Xcb/Wayland — fases de plataforma).
