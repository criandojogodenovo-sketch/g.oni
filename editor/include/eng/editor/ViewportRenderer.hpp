#pragma once

/// eng::editor::ViewportRenderer — desenho do viewport via eng::rhi
/// (FASE 8, missão §8.6; auditoria D3).
///
/// DECISÃO (ADR-042): a abstraction RHI não tem uniforms (FASE 4 — fora de
/// escopo então). O viewport v1 transforma vértices na CPU (world→clip em
/// C++) e envia um VBO dinâmico por updateBuffer — MESMO pipeline
/// pos+cor dos demos das FASES 5–7 (shaders embutidos com procedência).
/// Correto para a escala de um editor (centenas de quads); o gatilho de
/// revisão (uniforms no RHI) é quando o RENDER DE JOGO existir.
///
/// Conteúdo por frame: fundo cinza-escuro, grade (1 unidade; eixo mais
/// claro), um quad por entidade (tint determinístico), borda branca na
/// seleção, borda verde quando em Play.

#include <cstdint>
#include <optional>
#include <vector>

#include "eng/core/Result.hpp"
#include "eng/editor/Viewport.hpp"
#include "eng/rhi/Renderer.hpp"
#include "eng/rhi/Types.hpp"

namespace eng::editor {

class AssetBrowser;    // AssetBrowser.hpp
class TextureCache;    // TextureCache.hpp

class ViewportRenderer final {
public:
    ViewportRenderer() = default;
    ~ViewportRenderer();
    ViewportRenderer(ViewportRenderer&&) noexcept;
    ViewportRenderer& operator=(ViewportRenderer&&) noexcept;
    ViewportRenderer(const ViewportRenderer&) = delete;
    ViewportRenderer& operator=(const ViewportRenderer&) = delete;

    /// Cria o Renderer RHI (registro de fábricas é do HOST — EditorHost).
    /// Falha → erro preciso (ADR-036).
    [[nodiscard]] static eng::core::Result<ViewportRenderer> create(
        const eng::rhi::SurfaceDesc& surface,
        eng::rhi::BackendType backend);

    /// Redimensiona (surface mudou).
    eng::core::Result<void> resize(std::uint32_t width, std::uint32_t height);

    /// Um frame do viewport: quads + grade + seleção + partículas + SPRITES
    /// (texturas reais via TextureCache — evolução P0-3). `assets` nulo
    /// (sem projeto) = sprites caem no caminho de cor. false = não desenhou
    /// (minimizado/out-of-date persistente) — NUNCA lança.
    bool renderFrame(const Viewport& viewport,
                     const std::vector<EntityQuad>& quads,
                     const std::vector<ParticleQuad>& particles, bool playMode,
                     const AssetBrowser* assets, TextureCache& textures);

    /// Caminho legado (testes/hosts sem sprites) — sem texturas.
    bool renderFrame(const Viewport& viewport,
                     const std::vector<EntityQuad>& quads,
                     const std::vector<ParticleQuad>& particles, bool playMode);

    [[nodiscard]] bool isValid() const noexcept { return renderer_.has_value(); }
    [[nodiscard]] eng::rhi::BackendType activeBackend() const noexcept;
    [[nodiscard]] const eng::rhi::RendererCapabilities* capabilities() const
        noexcept;
    /// Acesso ao renderer interno (host destrói texturas do cache ANTES
    /// do renderer morrer — ADR-035; nullptr quando inválido/moved-from).
    [[nodiscard]] eng::rhi::Renderer* renderer() noexcept
    {
        return renderer_.has_value() ? &renderer_.value() : nullptr;
    }

    [[nodiscard]] std::uint64_t framesSubmitted() const noexcept
    {
        return framesSubmitted_;
    }
    [[nodiscard]] std::uint64_t framesPresented() const noexcept
    {
        return framesPresented_;
    }
    /// Vertex completo (pos vec4 + cor vec4) — formato das FASES 5–7.
    struct Vertex {
        float x, y, z, w;        ///< clip space (z=0, w=1)
        float r, g, b, a;
    };
    /// Vertex de sprite (pos vec4 + cor vec4 + uv vec2 — 40 bytes).
    struct SpriteVertex {
        float x, y, z, w;
        float r, g, b, a;
        float u, v;
    };
    static constexpr std::uint32_t kVerticesPerQuad = 6;

    /// Vértices do último frame (prova de conteúdo nos testes sem GPU).
    [[nodiscard]] std::size_t lastFrameVertexCount() const noexcept
    {
        return lastFrameVertexCount_;
    }
    /// Dados CPU construídos e ENVIADOS no último frame (clip space + cor) —
    /// exatamente o que a GPU recebeu. Leitura para testes/diagnóstico.
    [[nodiscard]] const std::vector<Vertex>& lastFrameVertices() const noexcept
    {
        return frameVertices_;
    }
    /// Vértices de SPRITE do último frame (clip + cor + UV).
    [[nodiscard]] const std::vector<SpriteVertex>& lastFrameSpriteVertices()
        const noexcept
    {
        return spriteVertices_;
    }
    /// Quants sprites foram desenhados com TEXTURA REAL no último frame.
    [[nodiscard]] std::size_t lastFrameTexturedSprites() const noexcept
    {
        return lastFrameTexturedSprites_;
    }

private:
    void destroyResources() noexcept;

    [[nodiscard]] bool ensureCapacity(std::size_t vertexCount);
    [[nodiscard]] bool ensureSpriteCapacity(std::size_t vertexCount);
    [[nodiscard]] bool buildAndDraw(const Viewport& viewport,
                                    const std::vector<EntityQuad>& quads,
                                    const std::vector<ParticleQuad>& particles,
                                    bool playMode, const AssetBrowser* assets,
                                    TextureCache* textures);

    std::optional<eng::rhi::Renderer> renderer_{};
    eng::rhi::ShaderHandle shader_{};
    eng::rhi::GraphicsPipelineHandle pipeline_{};
    eng::rhi::BufferHandle vertexBuffer_{};
    std::size_t vertexCapacity_ = 0;

    // --- sprite pipeline (evolução P0-3) --------------------------------------
    eng::rhi::ShaderHandle spriteShader_{};
    eng::rhi::GraphicsPipelineHandle spritePipeline_{};
    eng::rhi::BufferHandle spriteBuffer_{};
    std::size_t spriteCapacity_ = 0;
    std::vector<SpriteVertex> spriteVertices_{};
    std::size_t lastFrameTexturedSprites_ = 0;

    /// Arena de vértices do frame (reusada — zero alocação por frame após
    /// estabilizar; missão §10 mobile).
    std::vector<Vertex> frameVertices_{};

    std::uint64_t framesSubmitted_ = 0;
    std::uint64_t framesPresented_ = 0;
    std::size_t lastFrameVertexCount_ = 0;
};

} // namespace eng::editor
