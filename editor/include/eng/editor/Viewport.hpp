#pragma once

/// eng::editor::Viewport — câmera 2D, hit-test e lista de quads do editor
/// (FASE 8, missão §8.6; auditoria D3).
///
/// Modelo de coordenadas:
///   - MUNDO: unidades arbitrárias, Y para cima, X para direita;
///   - TELA: pixels, origem no canto superior esquerdo, Y para baixo
///     (convenção Android — missão §8.8);
///   - `screen = center + (world - cameraPos) * zoom`, com flip de Y.
///
/// Quads: cada nó vira um quad centralizado na posição-mundo derivada do
/// `computeWorldMatrix` do nó (hierarquia composta), tamanho = escala local
/// (clamp mínimo), rotação = ângulo no plano XY. CORRETO > COMPLEXO (§8.6):
/// é um marcador visual de entidade, não um renderer de jogo.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "eng/ecs/Ecs.hpp"
#include "eng/math/Mat4.hpp"
#include "eng/scene/Scene.hpp"

namespace eng::editor {

/// Um quad desenhável no viewport (dados, não comandos de GPU).
struct EntityQuad {
    eng::ecs::Entity entity{};
    float worldX{0.f};
    float worldY{0.f};
    float sizeX{1.f};              ///< em unidades de mundo (da escala)
    float sizeY{1.f};
    float rotation{0.f};           ///< radianos no plano XY
    std::uint32_t tint{0u};        ///< hue determinístico por entidade
    bool selected{false};
};

/// Quad de PARTÍCULA viva (marcador de gameplay — FASE 10). Auditoria
/// final: docs prometiam "o viewport desenha partículas como quads
/// (mesmo pipeline pos+cor)" e nada lia a ParticlePool — agora é real.
struct ParticleQuad {
    float worldX{0.f};
    float worldY{0.f};
    float size{0.08f};             ///< tamanho da partícula (mundo)
    float rotation{0.f};          ///< radianos no plano XY
};

class Viewport final {
public:
    /// Câmera 2D do editor (pan + zoom — §8.6).
    struct Camera2D {
        float posX{0.f};
        float posY{0.f};
        float zoom{48.f};          ///< pixels por unidade de mundo
    };

    // --- conversões --------------------------------------------------------

    [[nodiscard]] float worldToScreenX(float wx) const noexcept;
    [[nodiscard]] float worldToScreenY(float wy) const noexcept;
    [[nodiscard]] float screenToWorldX(float sx) const noexcept;
    [[nodiscard]] float screenToWorldY(float sy) const noexcept;

    // --- navegação (gestos — §8.6/§8.8) -------------------------------------

    /// Pan por delta de TELA (pixels).
    void pan(float screenDx, float screenDy) noexcept;

    /// Zoom centrado num foco de TELA (pinch). Fator > 1 = aproximar.
    void zoomAt(float factor, float screenFocusX, float screenFocusY) noexcept;

    // --- geometria do viewport ----------------------------------------------

    void setScreenSize(float width, float height) noexcept;
    [[nodiscard]] float screenWidth() const noexcept { return screenW_; }
    [[nodiscard]] float screenHeight() const noexcept { return screenH_; }
    [[nodiscard]] const Camera2D& camera() const noexcept { return camera_; }
    [[nodiscard]] Camera2D& camera() noexcept { return camera_; }

    /// Zoom clampado a limites utilizáveis (evita degenerar com pinch).
    static constexpr float kMinZoom = 8.f;
    static constexpr float kMaxZoom = 512.f;

    // --- conteúdo -----------------------------------------------------------

    /// Quads de TODOS os nós da cena, em ordem depth-first estável (a ordem
    /// de desenho; o hit-test percorre de trás para frente).
    [[nodiscard]] std::vector<EntityQuad> buildQuads(
        const eng::scene::Scene& scene,
        const std::optional<eng::ecs::Entity>& selection) const;

    /// Quads de TODAS as partículas vivas (uma por Particle de cada
    /// ParticlePool/emitter da cena). Desenhados POR CIMA das entidades —
    /// marcadores de gameplay, não selecionáveis.
    [[nodiscard]] std::vector<ParticleQuad> buildParticleQuads(
        const eng::scene::Scene& scene) const;

    /// Hit-test em coordenadas de TELA. Raio de tolerância em pixels
    /// (alvo de toque generoso — touch UX §8.8).
    [[nodiscard]] std::optional<eng::ecs::Entity> hitTest(
        const std::vector<EntityQuad>& quads, float screenX, float screenY,
        float touchRadius) const noexcept;

    /// Tamanho mínimo do quad em pixels (entidades pequenas continuam
    /// visíveis/toveicáveis em zoom baixo).
    static constexpr float kMinQuadPixels = 22.f;
    static constexpr float kQuadHalfWorld = 0.5f; ///< meia-largura padrão

    /// Quantos quads seriam desenhados (grade + entidades) — usado nos
    /// testes como prova de conteúdo sem GPU.
    [[nodiscard]] std::size_t quadCount(
        const std::vector<EntityQuad>& quads) const noexcept;

private:
    Camera2D camera_{};
    float screenW_{1.f};
    float screenH_{1.f};
};

} // namespace eng::editor
