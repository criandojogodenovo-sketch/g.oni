# eng::rhi::gles — Backend OpenGL ES real

- **Fase:** 6 (missão §32–§41) · ADR-038 · auditoria `docs/phase6_audit.md`
- **Target:** `eng_rhi_gles` (`engine/rhi/backends/gles/`) — linkado no
  executável final
- **Dependências:** `eng::rhi` (implementa `RhiBackend`), headers Khronos
  (FetchContent: EGL-Registry + OpenGL-Registry, commits fixados), `dl`

## O que é REAL aqui

| Requisito da missão | Implementação |
|---|---|
| Contexto via camada de plataforma (§33) | `EGL_PLATFORM_SURFACELESS_MESA` + `eglCreateContext` (3.2→3.1→3.0, mín. 3.0 real) |
| Versão real (§33) | `GL_VERSION`/`GLSL` consultados e reportados |
| Capabilities (§34) | limites/extensões/vendor/renderer consultados; softwareRendering honesto |
| Shaders (§35) | GLSL ES compilado/linkado de verdade; info logs completos nos erros |
| Recursos (§36) | VAO/VBO/EBO reais (apenas os necessários) |
| Pipeline (§37) | intenção → program + VAO + estado GL aplicado no bind |
| Frame (§38) | begin/draw/end/present reais (`eglSwapBuffers` em pbuffer); resize recria pbuffer; context/surface loss |
| Primeiro draw (§39) | triangle REAL com **readback de pixel verificado** (nível VALIDATED) |
| Paridade (§40) | MESMO vertex data/intenção nos dois backends no mesmo processo |

## Como rodar

```bash
# CI/Ubuntu: libEGL/libGLESv2 do sistema (mesa) — sem env extra.
# Sysroot local (EGL/GLES fora do sistema):
export LD_LIBRARY_PATH=.../sysroot/usr/lib/x86_64-linux-gnu
export __EGL_VENDOR_LIBRARY_FILENAMES=.../sysroot/usr/share/glvnd/egl_vendor.d/50_mesa.json
ctest --preset linux-debug -R rhi_gles --output-on-failure
```

Sem libEGL/libGLESv2 o teste **SKIPA com motivo** (missão §41). O teste de
paridade roda também o backend Vulkan — exporte `VK_ICD_FILENAMES`/
`VK_LAYER_PATH` (ou confira nos paths do sistema) para os dois lados.

## Diferenças reais vs Vulkan (documentadas, não escondidas)

| Aspecto | Vulkan (FASE 5) | OpenGL ES (FASE 6) |
|---|---|---|
| Shader | SPIR-V (validação magic) | GLSL ES (compilação/link runtime) |
| Upload de buffer | staging DEVICE_LOCAL + fence | `glBufferData/SubData` (driver gerencia) |
| Validation layers | Khronos (Enabled/Unavailable/…) | NÃO EXISTE em GLES → sempre `Unavailable` quando pedida |
| Wireframe | feature não habilitada (ADR-037) | INEXISTENTE em GLES 3.x (erro preciso) |
| Validação de output | layers + contadores reais (readback pendente) | **pixel lido e comparado** (§39) |
