# eng::rhi — Renderer Abstraction

- **Fase:** 4 (missão §4–§15) · ADR-035 (contrato) · ADR-036 (seleção)
- **Dependências:** `eng::core` (PUBLIC — Result/Error), `eng::log` (PRIVATE)
- **Backends:** [Vulkan (FASE 5, real)](14-rhi-vulkan.md) · [OpenGL ES (FASE 6, real)](15-rhi-gles.md)

## Objetivo

Interface de hardware gráfico de alto nível: o restante do motor consome
`Renderer`/handles e o modelo de frame, SEM conhecer Vulkan ou OpenGL ES.
A arquitetura resultante (missão §0):

```text
            Engine (scene/ecs/assets/jobs/...)
                          │
                          ▼
                 eng::rhi (Renderer Abstraction)
                    /           \
                   ▼             ▼
        rhi-vulkan (F5)   rhi-gles (F6)
```

Backends registram fábricas em `Renderer::registerBackend` e são linkados
NO EXECUTÁVEL FINAL — `eng_rhi` tem zero dependência gráfica (nem headers,
nem libs, nem transitivas).

## API essencial

```cpp
// registro (uma vez por processo, no executável final)
Renderer::registerBackend(BackendType::Vulkan, &createVulkanBackend);

// criação com seleção/validação (ADR-036)
RendererConfig config;                 // Auto | Vulkan | OpenGLES
config.surface.window = {win, Kind::Xcb};  // ausente → device-only
auto renderer = Renderer::create(config);  // Result<Renderer>

// recursos (handles opacos tipados, ADR-035)
auto vbo   = renderer->createBuffer({.size = 96, .usage = BufferUsage::Vertex});
auto shader = renderer->createShader({.vertexSpirv = ..., .fragmentGlsl = ...});

// frame (missão §12)
auto acquired = renderer->beginFrame();      // Result<AcquiredFrame>
if (acquired && acquired.value().status == FrameAcquireStatus::Renderable) {
    auto& frame = acquired.value().frame;
    frame.clear({});                          // Cor 0,0,0,1
    frame.setViewport({.width = 64, .height = 48});
    frame.setPipeline(pipeline);
    frame.bindVertexBuffer(vbo.value());
    frame.draw(3);
    frame.end();                              // submete
}
renderer->present();
```

## Modelo de estados

- **Disponibilidade** (ADR-036): `Unavailable → Detected → Available →
  Initialized → Capable → Presentable → Rendering → Validated`. Nível só é
  declarado quando VERIFICADO (missão §47).
- **Validação** (missão §18): `Enabled | DisabledByConfiguration |
  Unavailable | FailedToInitialize` — realidade, não desejo.
- **Frame**: `Renderable | OutOfDate | Minimized` são protocolo (não erro);
  `SurfaceLost`/sem-surface são erros precisos.

## Ownership e threading (ADR-035)

- `Renderer`/`Frame`: move-only, RAII; moved-from é defensivo (erro, não UB).
- Um `Frame` vivo mantém o backend vivo (estado compartilhado): destruir o
  `Renderer` durante gravação é seguro — o frame termina no próprio dtor.
- Recursos: owned pelo backend; destruição explícita ou em cascata.
- **Thread affinity**: single-threaded (uma thread de render); registry é
  thread-safe. Sem API async nesta fase.

## O que deliberadamente NÃO existe (missão §6)

Render graph, materiais, PBR, deferred, GPU-driven, hot reload,
pós-processamento, partículas, animação, editor, compute, samplers,
command lists genéricas, MRT, texturas (adiadas até fase que precise),
uniform buffers no pipeline, `assets::loadAsync`. Cada item entra quando
uma fase futura o exigir — sem APIs especulativas.

## Testes

`eng_rhi_tests` (15 casos / 386 asserções): registro/seleção (Auto, ordem,
fallback explícito, motivos agregados), validação honesta, capabilities
marcadas como fake, handles (nulo/stale/double-destroy/gerações), desc
validation no frontend, protocolo de frame (ordem exata, erros, RAII,
OutOfDate/Minimized), device-only × presentation, surface perdida/resize,
move semantics, teardown com liberação total. **FakeBackend vive apenas em
`engine/rhi/tests/`** (missão §11) — nunca é reportado como suporte real.
