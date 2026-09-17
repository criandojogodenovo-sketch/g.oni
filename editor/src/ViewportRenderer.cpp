#include "eng/editor/ViewportRenderer.hpp"

/// ViewportRenderer — pipeline pos+cor, VBO dinâmico CPU→clip (FASE 8, D3).

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "EditorShaders.hpp"
#include "eng/log/Macros.hpp"
#include "eng/rhi/RhiBackend.hpp"

namespace eng::editor {

namespace {

using eng::core::Error;
using eng::core::Result;
using eng::core::StatusCode;
using eng::core::makeUnexpected;

ENG_LOG_CATEGORY("editor");

[[nodiscard]] Error rendererError(StatusCode code, std::string message)
{
    return Error{code, "ViewportRenderer: " + std::move(message)};
}

/// Paleta do editor (HSV→RGB simples p/ tints determinísticos).
[[nodiscard]] float hueToRgb(float p, float q, float t) noexcept
{
    if (t < 0.f) { t += 1.f; }
    if (t > 1.f) { t -= 1.f; }
    if (t < 1.f / 6.f) { return p + (q - p) * 6.f * t; }
    if (t < 0.5f) { return q; }
    if (t < 2.f / 3.f) { return p + (q - p) * (2.f / 3.f - t) * 6.f; }
    return p;
}

void hsvToRgb(std::uint32_t hue, float& r, float& g,
                            float& b) noexcept
{
    const float h = static_cast<float>(hue % 360u) / 360.f;
    const float s = 0.58f;
    const float v = 0.92f;
    if (s <= 0.f) {
        r = g = b = v;
        return;
    }
    const float q = v < 0.5f ? v * (1.f + s) : v + s - v * s;
    const float p = 2.f * v - q;
    r = hueToRgb(p, q, h + 1.f / 3.f);
    g = hueToRgb(p, q, h);
    b = hueToRgb(p, q, h - 1.f / 3.f);
}

/// Um quad (2 triângulos) em clip space com cor uniforme e rotação.
void pushQuad(std::vector<ViewportRenderer::Vertex>& out, float cx, float cy,
              float halfW, float halfH, float rotation, float r, float g,
              float b)
{
    const float cosR = std::cos(rotation);
    const float sinR = std::sin(rotation);
    // Cantos em ordem CCW: (-,-), (+,-), (+,+), (-,+).
    const float px[4] = {-halfW, halfW, halfW, -halfW};
    const float py[4] = {-halfH, -halfH, halfH, halfH};
    float vx[4];
    float vy[4];
    for (int i = 0; i < 4; ++i) {
        vx[i] = cx + px[i] * cosR - py[i] * sinR;
        vy[i] = cy + px[i] * sinR + py[i] * cosR;
    }
    const ViewportRenderer::Vertex quad[6] = {
        {vx[0], vy[0], 0.f, 1.f, r, g, b, 1.f},
        {vx[1], vy[1], 0.f, 1.f, r, g, b, 1.f},
        {vx[2], vy[2], 0.f, 1.f, r, g, b, 1.f},
        {vx[0], vy[0], 0.f, 1.f, r, g, b, 1.f},
        {vx[2], vy[2], 0.f, 1.f, r, g, b, 1.f},
        {vx[3], vy[3], 0.f, 1.f, r, g, b, 1.f},
    };
    out.insert(out.end(), std::begin(quad), std::end(quad));
}

}  // namespace

// =============================================================================
// Ciclo de vida
// =============================================================================

ViewportRenderer::~ViewportRenderer()
{
    destroyResources();
}

ViewportRenderer::ViewportRenderer(ViewportRenderer&& other) noexcept
    : renderer_(std::move(other.renderer_)),
      shader_(std::exchange(other.shader_, {})),
      pipeline_(std::exchange(other.pipeline_, {})),
      vertexBuffer_(std::exchange(other.vertexBuffer_, {})),
      vertexCapacity_(std::exchange(other.vertexCapacity_, 0)),
      frameVertices_(std::move(other.frameVertices_)),
      framesSubmitted_(std::exchange(other.framesSubmitted_, 0)),
      framesPresented_(std::exchange(other.framesPresented_, 0)),
      lastFrameVertexCount_(std::exchange(other.lastFrameVertexCount_, 0))
{
}

ViewportRenderer& ViewportRenderer::operator=(ViewportRenderer&& other) noexcept
{
    if (this != &other) {
        destroyResources();
        renderer_ = std::move(other.renderer_);
        shader_ = std::exchange(other.shader_, {});
        pipeline_ = std::exchange(other.pipeline_, {});
        vertexBuffer_ = std::exchange(other.vertexBuffer_, {});
        vertexCapacity_ = std::exchange(other.vertexCapacity_, 0);
        frameVertices_ = std::move(other.frameVertices_);
        framesSubmitted_ = std::exchange(other.framesSubmitted_, 0);
        framesPresented_ = std::exchange(other.framesPresented_, 0);
        lastFrameVertexCount_ = std::exchange(other.lastFrameVertexCount_, 0);
    }
    return *this;
}

void ViewportRenderer::destroyResources() noexcept
{
    // Renderer vivo destrói os handles via backend (ADR-035: RAII central).
    if (renderer_.has_value()) {
        if (vertexBuffer_.isValid()) {
            (void)renderer_->destroyBuffer(vertexBuffer_);
        }
        if (pipeline_.isValid()) {
            (void)renderer_->destroyGraphicsPipeline(pipeline_);
        }
        if (shader_.isValid()) {
            (void)renderer_->destroyShader(shader_);
        }
    }
    vertexBuffer_ = {};
    pipeline_ = {};
    shader_ = {};
    vertexCapacity_ = 0;
}

Result<ViewportRenderer> ViewportRenderer::create(
    const eng::rhi::SurfaceDesc& surface, eng::rhi::BackendType backend)
{
    // As FÁBRICAS são registradas pelo host (EditorHost::create) — aqui
    // apenas a seleção (ADR-036).
    eng::rhi::RendererConfig config;
    config.backend = backend;
    config.enableValidation = true;
    config.allowFallback = false;
    config.surface = surface;
    config.applicationName = "goni.editor";
    auto renderer = eng::rhi::Renderer::create(config);
    if (renderer.isError()) {
        return makeUnexpected(renderer.error());
    }

    ViewportRenderer self;
    self.renderer_ = std::move(renderer.value());

    // Shader com AMBAS as representações (paridade FASES 4–6: o backend
    // que escolhe — Vulkan consome SPIR-V, GLES compila GLSL ES).
    eng::rhi::ShaderDesc shaderDesc;
    shaderDesc.debugName = "editor.viewport";
    shaderDesc.vertexSpirv = kEditorVertexSpirvBytes();
    shaderDesc.fragmentSpirv = kEditorFragmentSpirvBytes();
    shaderDesc.vertexGlsl = kEditorVertexGlsl;
    shaderDesc.fragmentGlsl = kEditorFragmentGlsl;
    auto shader = self.renderer_->createShader(shaderDesc);
    if (shader.isError()) {
        return makeUnexpected(shader.error());
    }
    self.shader_ = shader.value();

    // Pipeline 2D: pos vec4 + cor vec4, sem depth, sem cull (quads de UI).
    eng::rhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.shader = self.shader_;
    pipelineDesc.vertexLayout.bindings.push_back({0, sizeof(Vertex)});
    pipelineDesc.vertexLayout.attributes.push_back(
        {0, 0, 0, eng::rhi::Format::R32G32B32A32Sfloat});
    pipelineDesc.vertexLayout.attributes.push_back(
        {1, 0, 16, eng::rhi::Format::R32G32B32A32Sfloat});
    pipelineDesc.raster.cull = eng::rhi::CullMode::None;
    pipelineDesc.depth.test = false;
    pipelineDesc.depth.write = false;
    auto pipeline = self.renderer_->createGraphicsPipeline(pipelineDesc);
    if (pipeline.isError()) {
        (void)self.renderer_->destroyShader(self.shader_);
        return makeUnexpected(pipeline.error());
    }
    self.pipeline_ = pipeline.value();
    return self;
}

Result<void> ViewportRenderer::resize(std::uint32_t width,
                                      std::uint32_t height)
{
    if (!renderer_.has_value()) {
        return makeUnexpected(
            rendererError(StatusCode::InvalidState, "renderer inexistente"));
    }
    auto resized = renderer_->resize(width, height);
    if (resized.isError()) {
        return makeUnexpected(resized.error());
    }
    return {};
}

// =============================================================================
// Frame
// =============================================================================

bool ViewportRenderer::ensureCapacity(std::size_t vertexCount)
{
    if (!renderer_.has_value()) {
        return false;
    }
    if (vertexBuffer_.isValid() && vertexCapacity_ >= vertexCount) {
        return true;
    }

    // Crescimento ×1.5 amortizado; buffer grande o bastante para o frame.
    std::size_t capacity = vertexCapacity_ == 0 ? 1024 : vertexCapacity_;
    while (capacity < vertexCount) {
        capacity = capacity + capacity / 2;
    }

    eng::rhi::BufferDesc desc;
    desc.size = capacity * sizeof(Vertex);
    desc.usage = eng::rhi::BufferUsage::Vertex | eng::rhi::BufferUsage::CopyDst;
    auto buffer = renderer_->createBuffer(desc);
    if (buffer.isError()) {
        ENG_WARN("viewport: falha ao crescer VBO ({})",
                 buffer.error().message);
        return false;
    }
    if (vertexBuffer_.isValid()) {
        (void)renderer_->destroyBuffer(vertexBuffer_);
    }
    vertexBuffer_ = buffer.value();
    vertexCapacity_ = capacity;
    return true;
}

bool ViewportRenderer::buildAndDraw(const Viewport& viewport,
                                    const std::vector<EntityQuad>& quads,
                                    bool playMode)
{
    frameVertices_.clear();

    // --- grade ---------------------------------------------------------------
    // Linhas nos inteiros do mundo; passo 5 quando o zoom não comporta 1.
    const float w = viewport.screenWidth();
    const float h = viewport.screenHeight();
    auto worldToClipX = [&](float wx) {
        return (viewport.worldToScreenX(wx) / w) * 2.f - 1.f;
    };
    auto worldToClipY = [&](float wy) {
        return 1.f - (viewport.worldToScreenY(wy) / h) * 2.f;
    };
    const float step = viewport.camera().zoom < 14.f ? 5.f : 1.f;
    const float x0 = std::floor(viewport.screenToWorldX(0.f) / step) * step;
    const float x1 = viewport.screenToWorldX(w);
    const float y0 = std::floor(viewport.screenToWorldY(h) / step) * step;
    const float y1 = viewport.screenToWorldY(0.f);
    const float halfPxX = 0.7f / w;  // ~1.4px de espessura em clip
    const float halfPxY = 0.7f / h;
    const float kGridR = 0.20f;
    const float kGridG = 0.21f;
    const float kGridB = 0.24f;
    const float kAxisR = 0.30f;
    const float kAxisG = 0.31f;
    const float kAxisB = 0.35f;
    for (float gx = x0; gx <= x1; gx += step) {
        const bool axis = std::abs(gx) < 0.5f * step;
        const float cx = worldToClipX(gx);
        pushQuad(frameVertices_, cx, 0.f, halfPxX, 1.f, 0.f,
                 axis ? kAxisR : kGridR, axis ? kAxisG : kGridG,
                 axis ? kAxisB : kGridB);
    }
    for (float gy = y0; gy <= y1; gy += step) {
        const bool axis = std::abs(gy) < 0.5f * step;
        const float cy = worldToClipY(gy);
        pushQuad(frameVertices_, 0.f, cy, 1.f, halfPxY, 0.f,
                 axis ? kAxisR : kGridR, axis ? kAxisG : kGridG,
                 axis ? kAxisB : kGridB);
    }

    // --- entidades -------------------------------------------------------------
    // Ordem: play-indicator → seleção (borda) → quad. Sem blending: desenho
    // por sobreposição (ordem estável — quads em depth-first).
    const float zoom = viewport.camera().zoom;
    for (const EntityQuad& quad : quads) {
        const float cx = worldToClipX(quad.worldX);
        const float cy = worldToClipY(quad.worldY);
        const float halfW = std::max(quad.sizeX * zoom * 0.5f,
                                      Viewport::kMinQuadPixels) / w;
        const float halfH = std::max(quad.sizeY * zoom * 0.5f,
                                      Viewport::kMinQuadPixels) / h;

        float r = 0.f;
        float g = 0.f;
        float b = 0.f;
        hsvToRgb(quad.tint, r, g, b);

        if (playMode) {
            // Borda verde (estado Play visível — §8.7).
            pushQuad(frameVertices_, cx, cy, halfW + 8.f / w, halfH + 8.f / h,
                     quad.rotation, 0.10f, 0.75f, 0.35f);
        }
        if (quad.selected) {
            // Borda branca de seleção.
            pushQuad(frameVertices_, cx, cy, halfW + 4.f / w, halfH + 4.f / h,
                     quad.rotation, 0.95f, 0.96f, 0.98f);
        }
        pushQuad(frameVertices_, cx, cy, halfW, halfH, quad.rotation, r, g, b);
    }

    if (!ensureCapacity(frameVertices_.size())) {
        return false;
    }

    auto acquired = renderer_->beginFrame();
    if (acquired.isError()) {
        return false; // surface perdida — próximo frame re-tenta
    }
    if (acquired.value().status != eng::rhi::FrameAcquireStatus::Renderable) {
        return false;
    }
    eng::rhi::Frame& frame = acquired.value().frame;

    eng::rhi::ClearDesc clear;
    clear.color = {0.13f, 0.14f, 0.16f, 1.f};
    if (playMode) {
        clear.color = {0.10f, 0.13f, 0.11f, 1.f}; // tom levemente esverdeado
    }
    auto cleared = frame.clear(clear);
    auto viewportSet = frame.setViewport({0.f, 0.f, w, h, 0.f, 1.f});
    auto pipelined = frame.setPipeline(pipeline_);
    auto bound = frame.bindVertexBuffer(vertexBuffer_);
    // Upload do frame inteiro (offset 0) — VBO é dinâmico.
    auto uploaded = renderer_->updateBuffer(
        vertexBuffer_, 0,
        {reinterpret_cast<const std::byte*>(frameVertices_.data()),
         frameVertices_.size() * sizeof(Vertex)});
    auto drawn = frame.draw(
        static_cast<std::uint32_t>(frameVertices_.size()), 0);
    auto ended = frame.end();
    if (cleared.isError() || viewportSet.isError() || pipelined.isError() ||
        bound.isError() || uploaded.isError() || drawn.isError() ||
        ended.isError()) {
        ENG_WARN("viewport: comando rejeitado (frame abortado)");
        return false;
    }
    auto presented = renderer_->present();
    ++framesSubmitted_;
    lastFrameVertexCount_ = frameVertices_.size();
    if (presented.isError()) {
        ENG_WARN("viewport: present falhou ({})", presented.error().message);
        return true; // submetido mesmo assim (contadores honestos)
    }
    ++framesPresented_;
    return true;
}

bool ViewportRenderer::renderFrame(const Viewport& viewport,
                                   const std::vector<EntityQuad>& quads,
                                   bool playMode)
{
    // VBO nasce sob demanda no buildAndDraw (ensureCapacity) — validar
    // ANTES seria rejeitar o primeiro frame.
    if (!renderer_.has_value() || !pipeline_.isValid()) {
        return false;
    }
    return buildAndDraw(viewport, quads, playMode);
}

eng::rhi::BackendType ViewportRenderer::activeBackend() const noexcept
{
    return renderer_.has_value() ? renderer_->activeBackend()
                                : eng::rhi::BackendType::Auto;
}

const eng::rhi::RendererCapabilities* ViewportRenderer::capabilities() const
    noexcept
{
    return renderer_.has_value() ? &renderer_->capabilities() : nullptr;
}

}  // namespace eng::editor
