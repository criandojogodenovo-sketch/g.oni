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
#include <string>
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

    // --- sprite (evolução P0-3) — preenchido quando o nó tem SpriteData ---
    /// Nó TEM SpriteData (com ou sem textura). Sem textura → o renderer
    /// desenha o PLACEHOLDER xadrez (P1.10: claramente identificado,
    /// não confundir com sprite renderizado/hue de entidade crua).
    bool isSprite{false};
    /// Nome do asset de textura (vazio = quad de cor, caminho antigo).
    std::string textureAsset{};
    /// Região UV do sprite (respeita flip no renderer).
    float u0{0.f};
    float v0{0.f};
    float u1{1.f};
    float v1{1.f};
    /// Tint multiplicativo RGBA (1,1,1,1 = sem tint).
    float tintR{1.f};
    float tintG{1.f};
    float tintB{1.f};
    float tintA{1.f};
    bool flipX{false};
    bool flipY{false};
    /// Ordem de desenho (maior = frente — o renderer ordena sprites por isto).
    float sort{0.f};
    /// Pixels por unidade de mundo do sprite (do SpriteData).
    float spritePpu{1.f};
    /// Pivot do sprite [0..1] (0.5,0.5 = centrado).
    float pivotX{0.5f};
    float pivotY{0.5f};
    /// Dimensões em PIXELS da textura resolvida (0 = desconhecida — o
    /// hit-test usa o tamanho de sprite SÓ quando ambas > 0; o documento
    /// preenche via TextureCache::imageInfo antes do tap, o renderer via
    /// a textura GPU que subiu). Tamanho mundial do sprite =
    /// escala × (região em px / ppu) — o hit box tem de casar com o
    /// desenhado, senão o autor toca na imagem e "não seleciona nada".
    std::uint32_t textureWidthPx{0u};
    std::uint32_t textureHeightPx{0u};

    // --- collider (RECOVERY §10) — preenchido quando o nó tem Collider ---
    /// O AUTOR precisa VER o shape de colisão que está editando: o quad
    /// carrega a geometria (nas MESMAS convenções do PhysicsWorld —
    /// centrado no nó, escalado pelas colunas do world matrix) e o
    /// renderer desenha o contorno por cima da cena.
    bool hasCollider{false};
    /// Meia-largura/altura em MUNDO (esfera: halfX == halfY == raio).
    float colliderHalfX{0.5f};
    float colliderHalfY{0.5f};
    /// Esfera → contorno octogonal; box → retângulo na rotação do nó.
    bool colliderIsSphere{false};
    /// Trigger → contorno âmbar (contato SEM resolução — §7.2 da física).
    bool colliderTrigger{false};
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

    /// Pan por delta de TELA (pixels). NO-OP quando a câmera de JOGO
    /// está ativa (P0-5: em Play com câmera na cena, mexer na câmera do
    /// editor por trás seria debug mentiroso — a câmera é do jogo).
    void pan(float screenDx, float screenDy) noexcept;

    /// Zoom centrado num foco de TELA (pinch). Fator > 1 = aproximar.
    /// Mesma política de `pan` sob câmera de jogo.
    void zoomAt(float factor, float screenFocusX, float screenFocusY) noexcept;

    // --- geometria do viewport ----------------------------------------------

    void setScreenSize(float width, float height) noexcept;
    [[nodiscard]] float screenWidth() const noexcept { return screenW_; }
    [[nodiscard]] float screenHeight() const noexcept { return screenH_; }
    [[nodiscard]] const Camera2D& camera() const noexcept { return camera_; }
    [[nodiscard]] Camera2D& camera() noexcept { return camera_; }

    // --- câmera de jogo (evolução P0-5, ADR-051) ------------------------------

    /// Define a câmera de JOGO usada nas conversões (nullptr = desliga).
    /// TODAS as conversões world↔screen E o hit-test passam a usá-la — o
    /// render, o toque e o arraste seguem a câmera do jogo de graça.
    /// O DONO do objeto apontado é o chamador (o documento guarda o
    /// cache do frame). Pan/zoom do editor ficam no-op enquanto ativa.
    void setGameCamera(const Camera2D* camera) noexcept { gameCamera_ = camera; }
    [[nodiscard]] bool gameCameraActive() const noexcept
    {
        return gameCamera_ != nullptr;
    }
    /// Câmera em foco: a de jogo quando ativa, senão a do editor.
    [[nodiscard]] const Camera2D& effectiveCamera() const noexcept
    {
        return gameCamera_ != nullptr ? *gameCamera_ : camera_;
    }

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
    const Camera2D* gameCamera_ = nullptr;  ///< câmera de jogo (P0-5)
    float screenW_{1.f};
    float screenH_{1.f};
};

} // namespace eng::editor
