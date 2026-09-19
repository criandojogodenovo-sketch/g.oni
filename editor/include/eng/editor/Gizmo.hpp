#pragma once

/// eng::editor::TransformGizmo — gizmo 2D de transformação (P1).
///
/// Camada de DADOS/LÓGICA PURA (sem GPU, sem documento): recebe o
/// Viewport (conversões tela↔mundo) + os bounds da entidade
/// selecionada e devolve (a) hit-test dos handles, (b) transform alvo
/// durante o drag, (c) geometria de desenho (quads/segmentos em MUNDO)
/// para o ViewportRenderer.
///
/// Ferramentas (P1.6 — TOOL MODES): Select/Move/Rotate/Scale vivem no
/// EditorDocument; o gizmo só desenha/age nas três de transformação.
///
/// Convenções:
///   - posição/escala em unidades de MUNDO; rotação em GRAUS (a mesma
///     convenção Euler do EditorDocument::TransformDesc — Inspector em
///     graus, cena em Quat);
///   - handles têm tamanho CONSTANTE EM TELA (px convertidos a mundo
///     pelo zoom da câmera EM FOCO no momento da operação);
///   - arraste devolve o TRANSFORM ALVO (estado absoluto), não delta —
///     o chamador aplica. Uma fonte de verdade: o ECS.

#include <cstdint>
#include <vector>

#include "eng/editor/Viewport.hpp"

namespace eng::editor {

/// Ferramenta ativa do editor (P1.6). Pan/zoom continuam gestos de
/// navegação SEMPRE disponíveis (drag em espaço vazio / pinch) — não
/// são ferramentas de autoria.
enum class EditorTool : std::uint8_t {
    Select = 0, ///< tap seleciona; drag = pan
    Move,        ///< gizmo de movimento (eixo X, eixo Y, centro)
    Rotate,      ///< gizmo de rotação (anel + handle)
    Scale        ///< gizmo de escala (4 cantos)
};

/// Handle do gizmo — alvo do toque/drag (P1.3–P1.5).
enum class GizmoHandle : std::uint8_t {
    None = 0,
    MoveCenter,  ///< move livre (X+Y)
    MoveAxisX,   ///< constrain ao eixo X do mundo
    MoveAxisY,   ///< constrain ao eixo Y do mundo
    RotateRing,  ///< rotação (ângulo pointer↔pivot)
    ScaleNE,     ///< canto nordeste do bounds
    ScaleNW,
    ScaleSE,
    ScaleSW
};

/// Bounds da entidade selecionada no plano do MUNDO — layout do gizmo
/// e clamps de escala usam EXATAMENTE o tamanho desenhado (posição,
/// rotação, escala, textura e ppu — P1.2), nunca um tamanho arbitrário.
///
/// P2 (bug §5): dois pontos de referência distintos, ambos derivados do
/// estado ATUAL da entidade (nenhum dado temporário):
///   - worldX/worldY: CENTRO VISUAL (com offset de pivot do sprite) —
///     pivô do ROTATE, cantos do SCALE e desenho do gizmo;
///   - originX/originY: ORIGEM DO NÓ (translation do world matrix) —
///     alvo do MOVE (a posição que o Transform guarda).
struct GizmoBounds {
    float worldX{0.f};   ///< centro visual (posição + offset de pivot)
    float worldY{0.f};
    float originX{0.f};  ///< origem do NÓ em mundo (sem pivot — MOVE)
    float originY{0.f};
    float halfW{0.5f};   ///< MEIA-largura desenhada (mundo)
    float halfH{0.5f};
    float rotation{0.f}; ///< radianos no plano XY
    bool valid{false};   ///< false → gizmo não desagina nem acerta
};

/// Estado TRS que o gizmo lê/escreve (graus — convenção do Inspector).
/// P2: posX/posY são a posição de MUNDO da ORIGEM do nó — o DOCUMENTO
/// converte o delta de mundo para o espaço LOCAL do pai (filhos de pais
/// rotacionados/escalados movem no eixo de TELA certo).
struct GizmoTransform {
    float posX{0.f};
    float posY{0.f};
    float rotationDeg{0.f};
    float scaleX{1.f};
    float scaleY{1.f};
};

/// Quad preenchido do gizmo, em MUNDO (renderer converte p/ clip).
struct GizmoQuad {
    float worldX{0.f};
    float worldY{0.f};
    float halfW{1.f};
    float halfH{1.f};
    float rotation{0.f};
    float r{1.f};
    float g{1.f};
    float b{1.f};
};

/// Segmento do gizmo (eixos/anel), em MUNDO.
struct GizmoSegment {
    float x0{0.f};
    float y0{0.f};
    float x1{0.f};
    float y1{0.f};
    float r{1.f};
    float g{1.f};
    float b{1.f};
};

/// Pacote de desenho do gizmo (quads + segmentos em MUNDO) — o documento
/// produz, o renderer consome (P1).
struct GizmoDrawData {
    std::vector<GizmoQuad> quads;
    std::vector<GizmoSegment> segments;
};

class TransformGizmo final {
public:
    /// Dimensões de tela (px) — constantes em zoom (alvo de dedo §8.8).
    static constexpr float kHandlePx = 13.f;   ///< lado do handle
    static constexpr float kAxisPx = 84.f;     ///< comprimento do eixo
    static constexpr float kRingPadPx = 26.f;  ///< folga do anel p/ fora
    static constexpr float kHitPx = 24.f;      ///< raio de acerto (dedo)
    /// Snap de rotação: 15° com ímã de 4° (opcional, previsível).
    static constexpr float kRotateSnapStepDeg = 15.f;
    static constexpr float kRotateSnapPullDeg = 4.f;
    /// Escala — impedir valores inválidos (P1.5).
    static constexpr float kScaleMin = 0.01f;
    static constexpr float kScaleMax = 100.f;

    // --- hit-test -------------------------------------------------------------

    /// Handle sob o toque (px de tela). Invalid bounds / tool sem gizmo
    /// → None. Handles têm precedência sobre o corpo da entidade — a
    /// Activity consulta isto ANTES do viewportTap (P1.3).
    [[nodiscard]] GizmoHandle hitTest(const Viewport& viewport,
                                     EditorTool tool,
                                     const GizmoBounds& bounds,
                                     float screenX, float screenY) const;

    // --- drag (P1.3–P1.5) ------------------------------------------------------

    /// Captura o estado inicial (transform + ponto de agarre). O drag é
    /// uma operação ATÔMICA: begin → dragTo* → endDrag.
    void beginDrag(GizmoHandle handle, const GizmoTransform& startTransform,
                   const Viewport& viewport, const GizmoBounds& bounds,
                   float screenX, float screenY);

    /// Transform ALVO para a posição do pointer. Sem drag ativo →
    /// devolve o transform inicial inalterado.
    [[nodiscard]] GizmoTransform dragTo(const Viewport& viewport,
                                        const GizmoBounds& bounds,
                                        float screenX, float screenY) const;

    void endDrag() noexcept { active_ = GizmoHandle::None; }
    [[nodiscard]] GizmoHandle activeHandle() const noexcept
    {
        return active_;
    }
    [[nodiscard]] bool dragging() const noexcept
    {
        return active_ != GizmoHandle::None;
    }

    // --- desenho ----------------------------------------------------------------

    /// Geometria da ferramenta (em MUNDO) para o renderer. Vazia quando
    /// a ferramenta não tem gizmo (Select) ou bounds inválido.
    [[nodiscard]] std::vector<GizmoQuad> layoutQuads(
        const Viewport& viewport, EditorTool tool,
        const GizmoBounds& bounds) const;
    [[nodiscard]] std::vector<GizmoSegment> layoutSegments(
        const Viewport& viewport, EditorTool tool,
        const GizmoBounds& bounds) const;

    /// Cores canônicas (X vermelho, Y verde, centro amarelo, rotação
    /// ciano, escala âmbar — mesmas em hit-test, drag e desenho).
    static constexpr float kXAxisR = 0.92f, kXAxisG = 0.33f, kXAxisB = 0.28f;
    static constexpr float kYAxisR = 0.36f, kYAxisG = 0.80f, kYAxisB = 0.40f;
    static constexpr float kCenterR = 0.96f, kCenterG = 0.82f, kCenterB = 0.30f;
    static constexpr float kRotateR = 0.32f, kRotateG = 0.78f, kRotateB = 0.92f;
    static constexpr float kScaleR = 0.96f, kScaleG = 0.62f, kScaleB = 0.28f;

private:
    GizmoHandle active_ = GizmoHandle::None;
    GizmoTransform start_{};
    float grabWorldX_ = 0.f;   ///< ponto de agarre em MUNDO (move)
    float grabWorldY_ = 0.f;
    float startAngleRad_ = 0.f; ///< ângulo pointer↔pivot no begin (rotate)
    float startPointerLocalX_ = 1.f; ///< pointer no frame LOCAL do nó (scale)
    float startPointerLocalY_ = 1.f;
};

}  // namespace eng::editor
