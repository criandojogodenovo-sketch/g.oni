#include "eng/editor/ViewportRenderer.hpp"

/// ViewportRenderer — pipeline pos+cor, VBO dinâmico CPU→clip (FASE 8, D3).

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

#include "EditorShaders.hpp"
#include "eng/editor/AssetBrowser.hpp"
#include "eng/editor/TextureCache.hpp"
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

/// Segmento espesso (RECOVERY §10 — contornos de collider): um quad
/// girado de A a B com a espessura dada (half-thickness em clip).
void pushSegment(std::vector<ViewportRenderer::Vertex>& out, float ax,
                 float ay, float bx, float by, float halfThick, float r,
                 float g, float b)
{
    const float dx = bx - ax;
    const float dy = by - ay;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-9f) {
        return;
    }
    pushQuad(out, (ax + bx) * 0.5f, (ay + by) * 0.5f, length * 0.5f,
             halfThick, std::atan2(dy, dx), r, g, b);
}

/// Um quad de sprite LIT (2 triângulos, pos+cor+uv+MUNDO — P3 §5): o
/// fragment ilumina por DISTÂNCIA MUNDIAL (luzes do bloco PerFrame).
void pushLitSpriteQuad(std::vector<ViewportRenderer::LitSpriteVertex>& out,
                       float cx, float cy, float halfW, float halfH,
                       float rotation, float u0, float v0, float u1, float v1,
                       float r, float g, float b, float a, float worldX,
                       float worldY, float worldHalfW, float worldHalfH)
{
    const float cosR = std::cos(rotation);
    const float sinR = std::sin(rotation);
    const float px[4] = {-halfW, halfW, halfW, -halfW};
    const float py[4] = {-halfH, -halfH, halfH, halfH};
    const float uu[4] = {u0, u1, u1, u0};
    const float vv[4] = {v0, v0, v1, v1};
    float vx[4];
    float vy[4];
    for (int i = 0; i < 4; ++i) {
        vx[i] = cx + px[i] * cosR - py[i] * sinR;
        vy[i] = cy + px[i] * sinR + py[i] * cosR;
    }
    // Mundo: mesmo quad em unidades MUNDIAIS (também rotacionado).
    float wx[4];
    float wy[4];
    for (int i = 0; i < 4; ++i) {
        wx[i] = worldX + px[i] / halfW * worldHalfW * cosR -
                py[i] / halfH * worldHalfH * sinR;
        wy[i] = worldY + px[i] / halfW * worldHalfW * sinR +
                py[i] / halfH * worldHalfH * cosR;
    }
    const ViewportRenderer::LitSpriteVertex quad[6] = {
        {vx[0], vy[0], 0.f, 1.f, r, g, b, a, uu[0], vv[0], wx[0], wy[0]},
        {vx[1], vy[1], 0.f, 1.f, r, g, b, a, uu[1], vv[1], wx[1], wy[1]},
        {vx[2], vy[2], 0.f, 1.f, r, g, b, a, uu[2], vv[2], wx[2], wy[2]},
        {vx[0], vy[0], 0.f, 1.f, r, g, b, a, uu[0], vv[0], wx[0], wy[0]},
        {vx[2], vy[2], 0.f, 1.f, r, g, b, a, uu[2], vv[2], wx[2], wy[2]},
        {vx[3], vy[3], 0.f, 1.f, r, g, b, a, uu[3], vv[3], wx[3], wy[3]},
    };
    out.insert(out.end(), std::begin(quad), std::end(quad));
}

/// Um quad de sprite (2 triângulos, pos+cor+uv) em clip space.
void pushSpriteQuad(std::vector<ViewportRenderer::SpriteVertex>& out, float cx,
                    float cy, float halfW, float halfH, float rotation, float u0,
                    float v0, float u1, float v1, float r, float g, float b,
                    float a)
{
    const float cosR = std::cos(rotation);
    const float sinR = std::sin(rotation);
    // Cantos CCW com UV: (-,-)=uv00, (+,-)=uv10, (+,+)=uv11, (-,+)=uv01.
    const float px[4] = {-halfW, halfW, halfW, -halfW};
    const float py[4] = {-halfH, -halfH, halfH, halfH};
    const float uu[4] = {u0, u1, u1, u0};
    const float vv[4] = {v0, v0, v1, v1};
    float vx[4];
    float vy[4];
    for (int i = 0; i < 4; ++i) {
        vx[i] = cx + px[i] * cosR - py[i] * sinR;
        vy[i] = cy + px[i] * sinR + py[i] * cosR;
    }
    const ViewportRenderer::SpriteVertex quad[6] = {
        {vx[0], vy[0], 0.f, 1.f, r, g, b, a, uu[0], vv[0]},
        {vx[1], vy[1], 0.f, 1.f, r, g, b, a, uu[1], vv[1]},
        {vx[2], vy[2], 0.f, 1.f, r, g, b, a, uu[2], vv[2]},
        {vx[0], vy[0], 0.f, 1.f, r, g, b, a, uu[0], vv[0]},
        {vx[2], vy[2], 0.f, 1.f, r, g, b, a, uu[2], vv[2]},
        {vx[3], vy[3], 0.f, 1.f, r, g, b, a, uu[3], vv[3]},
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
      vertexBuffer_(std::exchange(other.vertexBuffer_, {})),
      vertexCapacity_(std::exchange(other.vertexCapacity_, 0)),
      shaders_(std::move(other.shaders_)),
      spriteVertices_(std::move(other.spriteVertices_)),
      lastFrameTexturedSprites_(std::exchange(other.lastFrameTexturedSprites_, 0)),
      litSpriteBuffer_(std::exchange(other.litSpriteBuffer_, {})),
      litSpriteCapacity_(std::exchange(other.litSpriteCapacity_, 0)),
      litSpriteVertices_(std::move(other.litSpriteVertices_)),
      frameUniformsSent_(std::move(other.frameUniformsSent_)),
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
        vertexBuffer_ = std::exchange(other.vertexBuffer_, {});
        vertexCapacity_ = std::exchange(other.vertexCapacity_, 0);
        shaders_ = std::move(other.shaders_);
        spriteVertices_ = std::move(other.spriteVertices_);
        lastFrameTexturedSprites_ = std::exchange(other.lastFrameTexturedSprites_, 0);
        litSpriteBuffer_ = std::exchange(other.litSpriteBuffer_, {});
        litSpriteCapacity_ = std::exchange(other.litSpriteCapacity_, 0);
        litSpriteVertices_ = std::move(other.litSpriteVertices_);
        frameUniformsSent_ = std::move(other.frameUniformsSent_);
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
    // ShaderLibrary PRIMEIRO (handles de shader/pipeline são dela — P3 §2).
    if (renderer_.has_value()) {
        if (litSpriteBuffer_.isValid()) {
            (void)renderer_->destroyBuffer(litSpriteBuffer_);
        }
        shaders_.destroy(*renderer_);
        if (vertexBuffer_.isValid()) {
            (void)renderer_->destroyBuffer(vertexBuffer_);
        }
    }
    vertexBuffer_ = {};
    vertexCapacity_ = 0;
    litSpriteBuffer_ = {};
    litSpriteCapacity_ = 0;
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

    // Shader Core (P3 §2): os shaders/pipelines do 2D vivem na
    // ShaderLibrary do eng::render (color/unlit/lit — MESMOS fixtures
    // canônicos de sempre + o par LIT novo com bloco PerFrame).
    auto shaders = eng::render::ShaderLibrary::create(*self.renderer_);
    if (shaders.isError()) {
        return makeUnexpected(shaders.error());
    }
    self.shaders_ = std::move(shaders.value());
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

/// VBO de sprites LIT (48B — P3 §5): mesmo crescimento amortizado.
bool ViewportRenderer::ensureLitSpriteCapacity(std::size_t vertexCount)
{
    if (!renderer_.has_value()) {
        return false;
    }
    if (litSpriteBuffer_.isValid() && litSpriteCapacity_ >= vertexCount) {
        return true;
    }
    std::size_t capacity = litSpriteCapacity_ == 0 ? 1024 : litSpriteCapacity_;
    while (capacity < vertexCount) {
        capacity = capacity + capacity / 2;
    }
    eng::rhi::BufferDesc desc;
    desc.size = capacity * sizeof(LitSpriteVertex);
    desc.usage = eng::rhi::BufferUsage::Vertex | eng::rhi::BufferUsage::CopyDst;
    auto buffer = renderer_->createBuffer(desc);
    if (buffer.isError()) {
        ENG_WARN("viewport: falha ao crescer VBO de sprites lit ({})",
                 buffer.error().message);
        return false;
    }
    if (litSpriteBuffer_.isValid()) {
        (void)renderer_->destroyBuffer(litSpriteBuffer_);
    }
    litSpriteBuffer_ = buffer.value();
    litSpriteCapacity_ = capacity;
    return true;
}

bool ViewportRenderer::ensureSpriteCapacity(std::size_t vertexCount)
{
    if (!renderer_.has_value()) {
        return false;
    }
    if (spriteBuffer_.isValid() && spriteCapacity_ >= vertexCount) {
        return true;
    }
    std::size_t capacity = spriteCapacity_ == 0 ? 1024 : spriteCapacity_;
    while (capacity < vertexCount) {
        capacity = capacity + capacity / 2;
    }
    eng::rhi::BufferDesc desc;
    desc.size = capacity * sizeof(SpriteVertex);
    desc.usage = eng::rhi::BufferUsage::Vertex | eng::rhi::BufferUsage::CopyDst;
    auto buffer = renderer_->createBuffer(desc);
    if (buffer.isError()) {
        ENG_WARN("viewport: falha ao crescer VBO de sprites ({})",
                 buffer.error().message);
        return false;
    }
    if (spriteBuffer_.isValid()) {
        (void)renderer_->destroyBuffer(spriteBuffer_);
    }
    spriteBuffer_ = buffer.value();
    spriteCapacity_ = capacity;
    return true;
}

bool ViewportRenderer::buildAndDraw(const Viewport& viewport,
                                    const std::vector<EntityQuad>& quads,
                                    const std::vector<ParticleQuad>& particles,
                                    bool playMode, const AssetBrowser* assets,
                                    TextureCache* textures,
                                    const GizmoDrawData* gizmo)
{
    frameVertices_.clear();
    spriteVertices_.clear();
    litSpriteVertices_.clear();     // P3: lote lit do frame
    frameUniformsSent_.clear();    // P3: blocos PerFrame enviados
    gizmoVertices_.clear();  // P1: acessores de teste não vazam frame velho
    lastFrameTexturedSprites_ = 0;

    // --- sprites resolvidos ANTES (agrupamento por textura p/ batching) -------
    // Resolve cada sprite com textura via TextureCache; os que falham caem
    // no caminho de cor (honesto: sem textura, marcador hue + log único).
    struct ResolvedSprite {
        const EntityQuad* quad{};
        const TextureCache::GpuTexture* gpu{};
    };
    std::vector<ResolvedSprite> resolvedSprites{};
    resolvedSprites.reserve(quads.size());
    std::vector<const EntityQuad*> untexturedQuads{};
    untexturedQuads.reserve(quads.size());
    for (const EntityQuad& quad : quads) {
        if (!quad.textureAsset.empty() && assets != nullptr && textures != nullptr) {
            if (const auto* gpu = textures->acquire(*assets, *renderer_,
                                                    quad.textureAsset)) {
                resolvedSprites.push_back({&quad, gpu});
                continue;
            }
        }
        untexturedQuads.push_back(&quad);
    }
    // Ordenação por sort (estável — empates mantêm a hierarquia).
    std::stable_sort(resolvedSprites.begin(), resolvedSprites.end(),
                     [](const ResolvedSprite& a, const ResolvedSprite& b) {
                         return a.quad->sort < b.quad->sort;
                     });

    // --- DrawList (P3 §4): a render world do frame — luzes/ambiente na
    // estrutura GENÉRICA do eng::render (o bloco PerFrame dos grupos lit
    // sai daqui: packUniformsFor). Sprites entram na sequência abaixo.
    eng::render::DrawList drawList;
    drawList.ambientR = 1.f;  // default honesto: sem luzes = look clássico
    drawList.ambientG = 1.f;
    drawList.ambientB = 1.f;
    drawList.ambientIntensity = 1.f;
    for (const EntityQuad& quad : quads) {
        if (!quad.hasLight) {
            continue;
        }
        eng::render::DrawList::LightItem light;
        light.worldX = quad.worldX;
        light.worldY = quad.worldY;
        light.radius = quad.lightRadius;
        light.intensity = quad.lightIntensity;
        light.r = quad.lightColorR;
        light.g = quad.lightColorG;
        light.b = quad.lightColorB;
        light.falloff = quad.lightFalloff;
        light.layer = quad.lightLayer;
        drawList.lights.push_back(std::move(light));
    }

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
    const float step =
        viewport.effectiveCamera().zoom < 14.f ? 5.f : 1.f;  // P0-5: câmera em foco
    const float x0 = std::floor(viewport.screenToWorldX(0.f) / step) * step;
    const float x1 = viewport.screenToWorldX(w);
    const float y0 = std::floor(viewport.screenToWorldY(h) / step) * step;
    const float y1 = viewport.screenToWorldY(0.f);
    // RECOVERY P0: 1 unidade NDC = w/2 px — meio-valor em clip de N pixels
    // é (N*2/w)/2 = N/w. A espessura de ~1.4px exige half = 1.4/w (antes
    // 0.7/w desenhava 0.7px — metade do que o comentário prometia).
    const float halfPxX = 1.4f / w;
    const float halfPxY = 1.4f / h;
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

    // --- entidades SEM textura + BORDAS de sprites (pipeline pos+cor) -------
    // Ordem: play-indicator → seleção (borda) → quad. Sem blending: desenho
    // por sobreposição (ordem estável — quads em depth-first).
    const float zoom = viewport.effectiveCamera().zoom;  // P0-5
    auto pushEntityMarkers = [&](const EntityQuad& quad, float halfW, float halfH) {
        const float cx = worldToClipX(quad.worldX);
        const float cy = worldToClipY(quad.worldY);
        if (playMode) {
            pushQuad(frameVertices_, cx, cy, halfW + 8.f / w, halfH + 8.f / h,
                     quad.rotation, 0.10f, 0.75f, 0.35f);
        }
        if (quad.selected) {
            pushQuad(frameVertices_, cx, cy, halfW + 4.f / w, halfH + 4.f / h,
                     quad.rotation, 0.95f, 0.96f, 0.98f);
        }
    };
    for (const EntityQuad* quadPtr : untexturedQuads) {
        const EntityQuad& quad = *quadPtr;
        // RECOVERY P0: tamanho mundial correto — escala N unidades = N*zoom
        // px na tela; half-extent NDC = total_px/w (o 0.5 espúrio desenhava
        // tudo com METADE do size e o hit box ficava 2× maior que o quad).
        const float halfW = std::max(quad.sizeX * zoom,
                                     Viewport::kMinQuadPixels) / w;
        const float halfH = std::max(quad.sizeY * zoom,
                                     Viewport::kMinQuadPixels) / h;

        pushEntityMarkers(quad, halfW, halfH);

        // P1.10 — PLACEHOLDER de sprite: SpriteData SEM textura vira
        // xadrez magenta/escuro (convenção clássica "sem textura"),
        // CLARAMENTE identificado — não é sprite renderizado nem o hue
        // de entidade crua. Entidades sem SpriteData seguem hue.
        if (quad.isSprite) {
            constexpr int kChecker = 4;  // células por eixo
            constexpr float kChessR = 0.55f, kChessG = 0.22f, kChessB = 0.55f;
            constexpr float kDarkR = 0.13f, kDarkG = 0.13f, kDarkB = 0.13f;
            // Base escura PRIMEIRO (painter: por baixo), xadrez por cima.
            pushQuad(frameVertices_, worldToClipX(quad.worldX),
                     worldToClipY(quad.worldY), halfW, halfH, quad.rotation,
                     kDarkR, kDarkG, kDarkB);
            const float cellW = halfW * 2.f / kChecker;
            const float cellH = halfH * 2.f / kChecker;
            const float cosR = std::cos(quad.rotation);
            const float sinR = std::sin(quad.rotation);
            for (int iy = 0; iy < kChecker; ++iy) {
                for (int ix = 0; ix < kChecker; ++ix) {
                    if (((ix + iy) & 1) == 0) {
                        continue;  // célula escura já é o fundo
                    }
                    const float lx =
                        -halfW + cellW * (static_cast<float>(ix) + 0.5f);
                    const float ly =
                        -halfH + cellH * (static_cast<float>(iy) + 0.5f);
                    pushQuad(frameVertices_,
                             worldToClipX(quad.worldX) + lx * cosR - ly * sinR,
                             worldToClipY(quad.worldY) + lx * sinR + ly * cosR,
                             cellW * 0.5f, cellH * 0.5f, quad.rotation,
                             kChessR, kChessG, kChessB);
                }
            }
            continue;
        }

        float r = 0.f;
        float g = 0.f;
        float b = 0.f;
        hsvToRgb(quad.tint, r, g, b);

        pushQuad(frameVertices_, worldToClipX(quad.worldX),
                 worldToClipY(quad.worldY), halfW, halfH, quad.rotation, r, g, b);
    }

    // --- partículas (drift D6 da FASE 10 — auditoria final) ------------------
    // Quads pequenos branco-âmbar POR CIMA das entidades: marcadores de
    // gameplay do estado de Play (pools só existem em runtime/clone). Tamanho
    // mínimo de 2px para permanecerem visíveis em zoom baixo.
    for (const ParticleQuad& particle : particles) {
        const float cx = worldToClipX(particle.worldX);
        const float cy = worldToClipY(particle.worldY);
        // RECOVERY P0: same correção de half-extent (total = size*zoom px).
        const float half = std::max(particle.size * zoom, 2.f) / w;
        const float halfY = std::max(particle.size * zoom, 2.f) / h;
        pushQuad(frameVertices_, cx, cy, half, halfY, particle.rotation,
                 1.f, 0.86f, 0.55f);
    }

    // --- COLLIDERS (RECOVERY §10): o autor VÊ o shape de colisão ----------
    // Contorno por cima da camada de cor (abaixo apenas de sprites com
    // blending — o +2px de inflação espreita ao redor do sprite, mesmo
    // padrão da borda de seleção). Teal = sólido; âmbar = trigger (sem
    // resolução — §7.2 da física). Box = retângulo na ROTAÇÃO do nó;
    // esfera = octógono (aproximação honesta num renderer de quads).
    // Geometria idêntica à que o PhysicsWorld usará no Play: o que o
    // autor vê é o que a física resolve.
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kSphereSides = 8;
        const float kSolidR = 0.16f, kSolidG = 0.90f, kSolidB = 0.85f;
        const float kTriggerR = 0.98f, kTriggerG = 0.78f, kTriggerB = 0.20f;
        const float inflateX = 2.f / w;
        const float inflateY = 2.f / h;
        const float halfThick = 0.75f / w;
        for (const EntityQuad& quad : quads) {
            if (!quad.hasCollider) {
                continue;
            }
            const float r =
                quad.colliderTrigger ? kTriggerR : kSolidR;
            const float g =
                quad.colliderTrigger ? kTriggerG : kSolidG;
            const float b =
                quad.colliderTrigger ? kTriggerB : kSolidB;
            const float cx = worldToClipX(quad.worldX);
            const float cy = worldToClipY(quad.worldY);
            // RECOVERY P0: colliderHalfX é MEIA-extensão MUNDIAL — o canto
            // fica a halfX*zoom px do centro → offset NDC = 2*px/w. O fator
            // 2 faltava e o contorno saía com METADE do tamanho físico (a
            // promessa do §10 é ver o shape que a FÍSICA resolve).
            const float hx =
                quad.colliderHalfX * zoom * 2.f / w + inflateX;
            const float hy =
                quad.colliderHalfY * zoom * 2.f / h + inflateY;
            if (quad.colliderIsSphere) {
                // Octógono (fase inicial na rotação do nó p/ consistência).
                float prevX = cx + hx * std::cos(quad.rotation);
                float prevY = cy + hy * std::sin(quad.rotation);
                for (int i = 1; i <= kSphereSides; ++i) {
                    const float angle =
                        quad.rotation +
                        (2.f * kPi * static_cast<float>(i)) /
                            static_cast<float>(kSphereSides);
                    const float nextX = cx + hx * std::cos(angle);
                    const float nextY = cy + hy * std::sin(angle);
                    pushSegment(frameVertices_, prevX, prevY, nextX, nextY,
                               halfThick, r, g, b);
                    prevX = nextX;
                    prevY = nextY;
                }
            } else {
                // Retângulo: 4 cantos girados pela rotação do nó.
                const float cosR = std::cos(quad.rotation);
                const float sinR = std::sin(quad.rotation);
                const float corners[4][2] = {
                    {-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}};
                float vx[4];
                float vy[4];
                for (int i = 0; i < 4; ++i) {
                    vx[i] = cx + corners[i][0] * cosR -
                            corners[i][1] * sinR;
                    vy[i] = cy + corners[i][0] * sinR +
                            corners[i][1] * cosR;
                }
                for (int i = 0; i < 4; ++i) {
                    const int next = (i + 1) % 4;
                    pushSegment(frameVertices_, vx[i], vy[i], vx[next],
                                vy[next], halfThick, r, g, b);
                }
            }
        }
    }

    // --- CÂMERA de jogo (P2 §11): retângulo da VISTA -----------------------
    // A MESMA conta do Play (entidade + offset, tela/zoom): o autor vê
    // EXATAMENTE o que a câmera cobriria. Ativa = contorno azul-ciano;
    // inativa = tracejado escuro (o renderer de quads aproxima com
    // cantos mais curtos — leitura honesta sem shader dedicado).
    {
        const float kCamR = 0.42f, kCamG = 0.66f, kCamB = 0.98f;
        const float kOffR = 0.30f, kOffG = 0.32f, kOffB = 0.36f;
        const float halfThick = 1.2f / w;
        for (const EntityQuad& quad : quads) {
            if (!quad.hasCamera) {
                continue;
            }
            const float r = quad.cameraActive ? kCamR : kOffR;
            const float g = quad.cameraActive ? kCamG : kOffG;
            const float b = quad.cameraActive ? kCamB : kOffB;
            const float cx = worldToClipX(quad.cameraCenterX);
            const float cy = worldToClipY(quad.cameraCenterY);
            const float hx = quad.cameraHalfW * zoom * 2.f / w;
            const float hy = quad.cameraHalfH * zoom * 2.f / h;
            const float corners[4][2] = {
                {-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}};
            float vx[4];
            float vy[4];
            for (int i = 0; i < 4; ++i) {
                vx[i] = cx + corners[i][0];
                vy[i] = cy + corners[i][1];
            }
            for (int i = 0; i < 4; ++i) {
                const int next = (i + 1) % 4;
                pushSegment(frameVertices_, vx[i], vy[i], vx[next],
                            vy[next], halfThick, r, g, b);
            }
        }
    }

    // --- EMISSOR de partículas (P2 §10): marcador editável ------------------
    // Quad roxo + SETA na direção de emissão: o authoring tem feedback
    // visual do que o ParticleTick fará no Play (direção do emitter no
    // frame do nó). Pools vivas continuam sendo quads no Play.
    if (!playMode) {  // em Play o que vale é a SIMULAÇÃO (partículas)
        const float kEmR = 0.72f, kEmG = 0.44f, kEmB = 0.98f;
        const float halfThick = 1.0f / w;
        for (const EntityQuad* quadPtr : untexturedQuads) {
            const EntityQuad& quad = *quadPtr;
            if (!quad.hasEmitter) {
                continue;
            }
            const float cx = worldToClipX(quad.worldX);
            const float cy = worldToClipY(quad.worldY);
            const float half = quad.emitterSize * zoom * 2.f / w;
            const float halfY = quad.emitterSize * zoom * 2.f / h;
            // Quad-marcador na rotação do nó.
            pushQuad(frameVertices_, cx, cy, half, halfY, quad.rotation,
                     kEmR * 0.35f, kEmG * 0.35f, kEmB * 0.35f);
            // Seta: direção (mundo) do emissor escalada ~2x o marcador.
            const float dirX = quad.emitterDirX;
            const float dirY = quad.emitterDirY;
            const float tipX = cx + dirX * half * 2.6f;
            const float tipY = cy - dirY * halfY * 2.6f;  // Y mundo ↑, clip ↓
            pushSegment(frameVertices_, cx + dirX * half, cy - dirY * halfY,
                        tipX, tipY, halfThick, kEmR, kEmG, kEmB);
            // Ponta da seta: duas pernas para trás±perpendicular (em
            // clip space — Y invertido: "trás" em mundo = +dirY em clip).
            const float legX = half * 0.6f;
            const float legYc = halfY * 0.6f;
            const float perpX = -dirY, perpY = dirX;
            pushSegment(frameVertices_, tipX, tipY,
                        tipX - dirX * legX - perpX * legX,
                        tipY + dirY * legYc + perpY * legYc,
                        halfThick, kEmR, kEmG, kEmB);
            pushSegment(frameVertices_, tipX, tipY,
                        tipX - dirX * legX + perpX * legX,
                        tipY + dirY * legYc - perpY * legYc,
                        halfThick, kEmR, kEmG, kEmB);
        }
    }

    // --- SPRITES (P3 §3/§5): vértices por SHADER do material. Runs
    // consecutivos (mesmo shader+camada+textura) agrupam draws — a ORDEM
    // por sort é preservada (painter's algorithm entre pipelines).
    struct SpriteRun {
        bool lit{false};
        const TextureCache::GpuTexture* gpu{};
        std::string layer{};
        std::uint32_t firstVertex{0};
        std::uint32_t vertexCount{0};
    };
    std::vector<SpriteRun> runs;
    auto runOpen = [&](bool lit, const TextureCache::GpuTexture* gpu,
                      const std::string& layer) -> SpriteRun& {
        SpriteRun run;
        run.lit = lit;
        run.gpu = gpu;
        run.layer = layer;
        run.firstVertex = lit
                              ? static_cast<std::uint32_t>(litSpriteVertices_.size())
                              : static_cast<std::uint32_t>(spriteVertices_.size());
        runs.push_back(std::move(run));
        return runs.back();
    };

    for (const ResolvedSprite& sprite : resolvedSprites) {
        const EntityQuad& quad = *sprite.quad;
        const float regionPx =
            static_cast<float>(sprite.gpu->width) * (quad.u1 - quad.u0);
        const float regionPy =
            static_cast<float>(sprite.gpu->height) * (quad.v1 - quad.v0);
        const float worldW = std::max(quad.sizeX * regionPx / quad.spritePpu,
                                      Viewport::kMinQuadPixels / zoom);
        const float worldH = std::max(quad.sizeY * regionPy / quad.spritePpu,
                                      Viewport::kMinQuadPixels / zoom);
        // Borda de seleção/play no pipeline de cor (embaixo do sprite).
        pushEntityMarkers(quad, worldW * zoom / w, worldH * zoom / h);

        // Pivot: centro do quad desloca ((pivot - 0.5) * tamanho) nos eixos
        // do sprite (respeita rotação).
        const float pivotOffX = (quad.pivotX - 0.5f) * worldW;
        const float pivotOffY = (quad.pivotY - 0.5f) * worldH;
        const float cosR = std::cos(quad.rotation);
        const float sinR = std::sin(quad.rotation);
        const float centerWX = quad.worldX + pivotOffX * cosR - pivotOffY * sinR;
        const float centerWY = quad.worldY + pivotOffX * sinR + pivotOffY * cosR;
        const float cx = worldToClipX(centerWX);
        const float cy = worldToClipY(centerWY);

        // UV com flip (região trocada por eixo flipado).
        const float u0 = quad.flipX ? quad.u1 : quad.u0;
        const float u1 = quad.flipX ? quad.u0 : quad.u1;
        // v: origem do UV no canto SUPERIOR da imagem (stb/GL) — flipY
        // inverte a região verticalmente.
        const float v0 = quad.flipY ? quad.v1 : quad.v0;
        const float v1 = quad.flipY ? quad.v0 : quad.v1;

        // RECOVERY P0: half-extent NDC = total_px/w — worldW*zoom é o
        // total em px. O 0.5 espúrio desenhava a IMAGEM com metade do
        // tamanho da própria borda de seleção (borda correta, imagem não).
        const bool lit = quad.materialShader == "lit";

        // Run: abre ANTES do push — firstVertex é o BASE do sprite neste
        // run (abrir depois capturava size() pós-push e desenhava a
        // região ERRADA do VBO: draw(count, base+6) lia vértices que não
        // são do sprite — bug do refactor P3, achado no CI pelo readback).
        const bool runMatches =
            !runs.empty() && runs.back().lit == lit &&
            runs.back().gpu == sprite.gpu && runs.back().layer == quad.layer;
        if (!runMatches) {
            runOpen(lit, sprite.gpu, quad.layer);
        }

        if (lit) {
            // LIT (P3 §5): + posição MUNDO interpolada (attribute 3) para
            // a distância às luzes no fragment.
            pushLitSpriteQuad(litSpriteVertices_, cx, cy, worldW * zoom / w,
                              worldH * zoom / h, quad.rotation, u0, v0, u1,
                              v1, quad.tintR, quad.tintG, quad.tintB,
                              quad.tintA, quad.worldX, quad.worldY, worldW,
                              worldH);
        } else {
            pushSpriteQuad(spriteVertices_, cx, cy, worldW * zoom / w,
                          worldH * zoom / h, quad.rotation, u0, v0, u1, v1,
                          quad.tintR, quad.tintG, quad.tintB, quad.tintA);
        }
        ++lastFrameTexturedSprites_;

        // Item na DrawList (§4 — a render world genérica; mesmos dados
        // que os vértices, para consumidores futuros/runtime).
        eng::render::SpriteDrawItem item;
        item.worldX = quad.worldX;
        item.worldY = quad.worldY;
        item.rotation = quad.rotation;
        item.scaleX = quad.sizeX;
        item.scaleY = quad.sizeY;
        item.u0 = quad.u0;
        item.v0 = quad.v0;
        item.u1 = quad.u1;
        item.v1 = quad.v1;
        item.tintR = quad.tintR;
        item.tintG = quad.tintG;
        item.tintB = quad.tintB;
        item.tintA = quad.tintA;
        item.sort = quad.sort;
        item.pivotX = quad.pivotX;
        item.pivotY = quad.pivotY;
        item.flipX = quad.flipX;
        item.flipY = quad.flipY;
        item.spritePpu = quad.spritePpu;
        item.texture = quad.textureAsset;
        item.shader = quad.materialShader;
        item.layer = quad.layer;
        drawList.sprites.push_back(std::move(item));
        runs.back().vertexCount += kVerticesPerQuad;
    }

    if (!ensureCapacity(frameVertices_.size())) {
        return false;
    }
    if (!spriteVertices_.empty() && !ensureSpriteCapacity(spriteVertices_.size())) {
        return false;
    }
    if (!litSpriteVertices_.empty() &&
        !ensureLitSpriteCapacity(litSpriteVertices_.size())) {
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
    bool frameOk = true;
    auto cleared = frame.clear(clear);
    frameOk = frameOk && cleared.ok();
    auto viewportSet = frame.setViewport({0.f, 0.f, w, h, 0.f, 1.f});
    frameOk = frameOk && viewportSet.ok();

    // Lote 1: quads de cor (grade/entidades/bordas/partículas).
    if (frameOk && !frameVertices_.empty()) {
        auto pipelined = frame.setPipeline(shaders_.colorPipeline());
        auto bound = frame.bindVertexBuffer(vertexBuffer_);
        auto uploaded = renderer_->updateBuffer(
            vertexBuffer_, 0,
            {reinterpret_cast<const std::byte*>(frameVertices_.data()),
             frameVertices_.size() * sizeof(Vertex)});
        auto drawn = frame.draw(
            static_cast<std::uint32_t>(frameVertices_.size()), 0);
        frameOk = pipelined.ok() && bound.ok() && uploaded.ok() && drawn.ok();
    }

    // Lote 2 (P3 §3/§5): sprites POR RUN — ordem preservada (painter's),
    // pipeline por SHADER do material, luzes por CAMADA (bloco PerFrame
    // re-sobe quando a camada do run muda), textura bind 1× por run.
    // Batching real: bind/draw por GRUPO, nunca por sprite.
    if (frameOk && !runs.empty()) {
        bool anyLit = false;
        for (const SpriteRun& run : runs) {
            anyLit = anyLit || run.lit;
        }
        // Uploads dos VBOs usados (uma vez por frame e por buffer).
        if (anyLit) {
            auto uploaded = renderer_->updateBuffer(
                litSpriteBuffer_, 0,
                {reinterpret_cast<const std::byte*>(litSpriteVertices_.data()),
                 litSpriteVertices_.size() * sizeof(LitSpriteVertex)});
            frameOk = frameOk && uploaded.ok();
        }
        if (frameOk && !spriteVertices_.empty()) {
            auto uploaded = renderer_->updateBuffer(
                spriteBuffer_, 0,
                {reinterpret_cast<const std::byte*>(spriteVertices_.data()),
                 spriteVertices_.size() * sizeof(SpriteVertex)});
            frameOk = frameOk && uploaded.ok();
        }

        // Uniforms por CAMADA (P3 §5 — mask real): o bloco da camada é
        // empacotado UMA vez e re-bindado quando a camada do run muda.
        std::string boundLayer{};
        bool layerBound = false;

        bool pipelineIsLit = false;
        bool pipelineSet = false;
        bool bufferIsLit = true;
        bool bufferSet = false;
        const TextureCache::GpuTexture* boundGpu = nullptr;
        for (const SpriteRun& run : runs) {
            if (!frameOk) {
                break;
            }
            // Pipeline (shader do material) — troca só quando muda.
            if (!pipelineSet || pipelineIsLit != run.lit) {
                auto pipelined = frame.setPipeline(
                    run.lit ? shaders_.spriteLitPipeline()
                            : shaders_.spriteUnlitPipeline());
                frameOk = frameOk && pipelined.ok();
                pipelineIsLit = run.lit;
                pipelineSet = true;
                layerBound = false;  // pipeline novo: uniforms re-bindam
            }
            // VBO do shader — troca só quando muda.
            if (!bufferSet || bufferIsLit != run.lit) {
                auto bound = frame.bindVertexBuffer(
                    run.lit ? litSpriteBuffer_ : spriteBuffer_);
                frameOk = frameOk && bound.ok();
                bufferIsLit = run.lit;
                bufferSet = true;
            }
            // Luzes da CAMADA (só lit — unlit não consome o bloco).
            if (run.lit && (!layerBound || boundLayer != run.layer)) {
                const auto block = drawList.packUniformsFor(run.layer);
                auto uniformed = shaders_.bindFrameUniforms(frame, block);
                frameOk = frameOk && uniformed.ok();
                boundLayer = run.layer;
                layerBound = true;
                frameUniformsSent_.push_back(block);  // prova de conteúdo
            }
            // Textura do run.
            if (run.gpu != nullptr && run.gpu != boundGpu) {
                auto boundTexture =
                    frame.bindTexture(run.gpu->texture, run.gpu->sampler, 0);
                frameOk = frameOk && boundTexture.ok();
                boundGpu = run.gpu;
            }
            auto drawn =
                frame.draw(run.vertexCount, run.firstVertex);
            frameOk = frameOk && drawn.ok();
        }
    }

    // Lote 3 (P1): GIZMO — quads preenchidos + segmentos de eixo/anel NO
    // PIPELINE DE COR, POR CIMA de tudo (a entidade selecionada precisa
    // dos handles visíveis sobre a própria arte). Sem blending: handles
    // opacos com meia-borda de separação (painter's).
    if (frameOk && gizmo != nullptr &&
        (!gizmo->quads.empty() || !gizmo->segments.empty())) {
        gizmoVertices_.clear();
        for (const GizmoQuad& quad : gizmo->quads) {
            pushQuad(gizmoVertices_, worldToClipX(quad.worldX),
                     worldToClipY(quad.worldY),
                     quad.halfW * zoom / w, quad.halfH * zoom / h,
                     quad.rotation, quad.r, quad.g, quad.b);
        }
        for (const GizmoSegment& segment : gizmo->segments) {
            pushSegment(gizmoVertices_, worldToClipX(segment.x0),
                        worldToClipY(segment.y0), worldToClipX(segment.x1),
                        worldToClipY(segment.y1), 1.5f / w, segment.r,
                        segment.g, segment.b);
        }
        if (!gizmoVertices_.empty() && ensureCapacity(gizmoVertices_.size())) {
            auto pipelined = frame.setPipeline(shaders_.colorPipeline());
            auto bound = frame.bindVertexBuffer(vertexBuffer_);
            auto uploaded = renderer_->updateBuffer(
                vertexBuffer_, 0,
                {reinterpret_cast<const std::byte*>(gizmoVertices_.data()),
                 gizmoVertices_.size() * sizeof(Vertex)});
            auto drawn = frame.draw(
                static_cast<std::uint32_t>(gizmoVertices_.size()), 0);
            frameOk = pipelined.ok() && bound.ok() && uploaded.ok() &&
                      drawn.ok();
        } else if (!gizmoVertices_.empty()) {
            frameOk = false;  // VBO não cresceu — frame aborta (honesto)
        }
    }

    auto ended = frame.end();
    frameOk = frameOk && ended.ok();
    if (!frameOk) {
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
                                   const std::vector<ParticleQuad>& particles,
                                   bool playMode)
{
    // VBO nasce sob demanda no buildAndDraw (ensureCapacity) — validar
    // ANTES seria rejeitar o primeiro frame.
    if (!renderer_.has_value() || !shaders_.valid()) {
        return false;
    }
    return buildAndDraw(viewport, quads, particles, playMode, nullptr,
                        nullptr, nullptr);
}

bool ViewportRenderer::renderFrame(const Viewport& viewport,
                                   const std::vector<EntityQuad>& quads,
                                   const std::vector<ParticleQuad>& particles,
                                   bool playMode, const AssetBrowser* assets,
                                   TextureCache& textures,
                                   const GizmoDrawData* gizmo)
{
    if (!renderer_.has_value() || !shaders_.valid()) {
        return false;
    }
    return buildAndDraw(viewport, quads, particles, playMode, assets,
                        &textures, gizmo);
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
