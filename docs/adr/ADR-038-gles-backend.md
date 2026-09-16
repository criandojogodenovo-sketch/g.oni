# ADR-038 — Backend OpenGL ES: loader dinâmico, pbuffer headless, VAO no draw

- **Estado:** aceito (FASE 6, missão §32–§41)
- **Contexto:** implementar OpenGL ES REAL com paridade arquitetural ao
  backend Vulkan (ADR-037), mantendo o build hermético e a CI verde em
  ambiente headless.

## Decisão

### Loader por dlopen (padrão ADR-037)

`GlesLoader` abre `libEGL.so.1` + `libGLESv2.so.2` (`libEGL`/`libGLESv2` no
Android — missão §30) e resolve ~57 funções via `dlsym`. Os headers Khronos
(EGL-Registry/OpenGL-Registry) NÃO exportam typedefs `PFN_*` (diferente de
vulkan.h) — as assinaturas canônicas são declaradas no `GlesLoader.hpp`.
Headers via FetchContent com COMMITS fixados por `URL_HASH` (os repos não
publicam tags estáveis). `EGL_NO_X11` evita qualquer header de plataforma.

### Contexto: surfaceless + pbuffer (missão §33/§38)

- Display via `EGL_PLATFORM_SURFACELESS_MESA` — headless REAL: ausência de
  janela ≠ ausência de GLES (missão §13);
- versão de contexto: tenta **3.2 → 3.1 → 3.0** (mínimo suportado **3.0**,
  detectado e reportado em capabilities — missão §33);
- present REAL: `eglSwapBuffers` sobre **pbuffer** RGBA8888;
- device-only: contexto current SEM surface (surfaceless context) —
  recursos e capabilities funcionam, frames retornam erro preciso;
- `resize()` recria o pbuffer preservando o contexto; `EGL_CONTEXT_LOST` /
  `EGL_BAD_SURFACE` mapeiam para `surfaceLost()` + erro preciso (missão §38).

### Shaders GLSL ES com info logs (missão §35)

`createShader` exige `vertexGlsl`/`fragmentGlsl` (simétrico ao Vulkan que
exige SPIR-V — prova viva de paridade §40). Compilação e link REAIS com
`glGetShaderiv`/`glGetShaderInfoLog`/`glGetProgramiv`/`glGetProgramInfoLog`
SEMPRE verificados; o log completo viaja no `Error` do `Result`.
Descoberta real desta fase: **GLSL ES 3.00 não permite locations explícitos
inter-stage** (só em 3.10+/`GL_EXT_separate_shader_objects`) — o fixture
vincula varyings por NOME (documentado em `tests/shaders/`).

### Estado: intenção aplicada no bind (missão §37)

`GraphicsPipelineDesc` → program + VAO + raster/depth/blend aplicados em
`frameSetPipeline` (GLES é state machine). **Correção estrutural**
(descoberta com crash REAL no llvmpipe): `glVertexAttribPointer` captura o
buffer bound à GL_ARRAY_BUFFER NO MOMENTO da chamada — como a pipeline é
criada antes de qualquer VBO, os atributos são configurados NO DRAW (com o
VBO do frame bound), ordem-independente (pipeline/vbo em qualquer ordem).
Esse é o modelo clássico GL; DSA não existe em GLES 3.x.

### Buffers (missão §36)

VBO/EBO via `glGenBuffers/glBufferData` com `glBufferSubData` para updates —
SEM staging (semântica síncrona do GL; diferença REAL documentada, não
escondida: o Vulkan faz staging DEVICE_LOCAL + fence, o GLES deixa o driver
gerenciar). Índice `Uint16/Uint32` mapeado em `drawIndexed`.

### Capabilities e honestidade (missão §34/§47)

`GL_VERSION/VENDOR/RENDERER/GLSL` + limites consultados; formatos = conjunto
core ES 3.0 (exigidos pela spec — reais). `softwareRendering=true` quando o
renderer é llvmpipe/softpipe/SwiftShader. **ValidationState: OpenGL ES não
possui layers** — pedida → `Unavailable` com WARN, nunca `Enabled`
(missão §18). `wireframe=false`: GLES 3.x não tem polygon mode (diferença
real vs desktop GL; pedido → erro preciso).

### Readback de pixels (missão §39)

`readCenterPixel` (`glReadPixels`) valida o OUTPUT de verdade — o teste do
triangle lê o pixel central e compara com a cor esperada (nível VALIDATED).
O teste de PARIDADE (missão §40) executa o MESMO vertex data + a mesma
intenção nos DOIS backends reais no mesmo processo, com os fixtures
`triangle_{vk,gles}.*`.

### LSan × Mesa (testes)

Arenas JIT do llvmpipe (frames "unknown module"), libxshmfence (puxada pela
WSI mesmo em headless) e o runtime do sanitizer retêm alocações —
suppressions POR ORIGEM em `tests/mesa_lsan.supp`; leaks do código do MOTOR
(symbolizados arquivo:linha) permanecem detectáveis.

## Consequências

1. `beginFrame` aplica clear PRETO default (paridade observável com o
   loadOp=CLEAR do Vulkan — auditoria F6 §3.2); `frameClear` sobrepõe.
2. Uma sessão de gravação por vez (protocolo ADR-035); `present()` drena
   todos os submetidos pendentes (L3).
3. Uniform buffers/storage buffers/texturas na abstraction continuam
   adiados (missão §6) — entram com os consumidores reais.
4. Desktop surfaces (Xcb/Wayland) e Android ficam para as fases de
   plataforma (a cargo de `NativeWindowKind` já mapeado).
