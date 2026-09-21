#include "eng/editor/Gizmo.hpp"

/// TransformGizmo — implementação (P1; P4.1 re-trabalho de usabilidade).
///
/// Hit-test + matemática de drag + layout de desenho. Ver header para
/// as decisões (handles constantes em TELA escalados pela densidade;
/// drag devolve estado ALVO; bounds = tamanho desenhado da entidade).
///
/// P4.1 (D1–D4): alvos de toque em dp (≥48dp de diâmetro), anel de
/// rotação com raio mínimo de 64 px, MOVE com 4 setas + quadrado
/// central, ROTATE com handle visível + ponta de seta no anel, SCALE
/// com 4 cantos + 4 marcas de aresta (escala de UM eixo).

#include <algorithm>
#include <cmath>
#include <utility>

namespace eng::editor {

namespace {

constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] float pxToWorld(const Viewport& viewport, float px) noexcept
{
    const float zoom = viewport.effectiveCamera().zoom;
    return zoom > 0.f ? px / zoom : 0.f;
}

/// Distância em TELA entre (screenX,screenY) e um ponto de MUNDO.
[[nodiscard]] float screenDistanceTo(const Viewport& viewport, float worldX,
                                     float worldY, float screenX,
                                     float screenY) noexcept
{
    const float dx = viewport.worldToScreenX(worldX) - screenX;
    const float dy = viewport.worldToScreenY(worldY) - screenY;
    return std::sqrt(dx * dx + dy * dy);
}

/// Ponto do anel de rotação no ângulo atual da entidade (o handle nasce
/// "amarrado" à rotação — girar é pegar e balançar, sem salto).
[[nodiscard]] std::pair<float, float> ringHandleWorld(const GizmoBounds& b,
                                                     float radiusWorld) noexcept
{
    const float x = b.worldX + radiusWorld * std::cos(b.rotation);
    const float y = b.worldY + radiusWorld * std::sin(b.rotation);
    return {x, y};
}

/// Cantos do bounds GIRADOS pela rotação da entidade (P1.5: os handles
/// de escala seguem o retângulo real desenhado, não o AABB).
struct CornerPoints {
    std::pair<float, float> ne, nw, se, sw;
};

[[nodiscard]] CornerPoints cornersOf(const GizmoBounds& b) noexcept
{
    const float c = std::cos(b.rotation);
    const float s = std::sin(b.rotation);
    auto corner = [&](float lx, float ly) {
        return std::pair<float, float>{
            b.worldX + lx * c - ly * s, b.worldY + lx * s + ly * c};
    };
    return CornerPoints{corner(b.halfW, b.halfH), corner(-b.halfW, b.halfH),
                        corner(b.halfW, -b.halfH), corner(-b.halfW, -b.halfH)};
}

/// Centro das ARESTAS do bounds na rotação da entidade (P4.1/D4 — os
/// novos handles de escala de um eixo: E(+halfW,0) W(-halfW,0)
/// N(0,+halfH) S(0,-halfH)).
struct EdgePoints {
    std::pair<float, float> e, w, n, s;
};

[[nodiscard]] EdgePoints edgesOf(const GizmoBounds& b) noexcept
{
    const float c = std::cos(b.rotation);
    const float s = std::sin(b.rotation);
    auto point = [&](float lx, float ly) {
        return std::pair<float, float>{
            b.worldX + lx * c - ly * s, b.worldY + lx * s + ly * c};
    };
    return EdgePoints{point(b.halfW, 0.f), point(-b.halfW, 0.f),
                      point(0.f, b.halfH), point(0.f, -b.halfH)};
}

/// Snap de rotação: múltiplos de kRotateSnapStepDeg com ímã de
/// kRotateSnapPullDeg (P1.4 — previsível, desligável por distância).
[[nodiscard]] float snappedDegrees(float degrees) noexcept
{
    const float step = TransformGizmo::kRotateSnapStepDeg;
    const float pull = TransformGizmo::kRotateSnapPullDeg;
    const float nearest = std::round(degrees / step) * step;
    return (std::abs(degrees - nearest) <= pull) ? nearest : degrees;
}

/// Clamp de escala — impedir valores inválidos (P1.5).
[[nodiscard]] float clampScale(float value) noexcept
{
    if (!std::isfinite(value)) {
        return 1.f;
    }
    return std::clamp(value, TransformGizmo::kScaleMin, TransformGizmo::kScaleMax);
}

/// true quando o handle é um dos cantos do SCALE.
[[nodiscard]] bool isCornerHandle(GizmoHandle h) noexcept
{
    return h == GizmoHandle::ScaleNE || h == GizmoHandle::ScaleNW ||
           h == GizmoHandle::ScaleSE || h == GizmoHandle::ScaleSW;
}

/// true quando o handle é uma aresta do SCALE (um eixo só).
[[nodiscard]] bool isEdgeHandle(GizmoHandle h) noexcept
{
    return h == GizmoHandle::ScaleEdgeE || h == GizmoHandle::ScaleEdgeW ||
           h == GizmoHandle::ScaleEdgeN || h == GizmoHandle::ScaleEdgeS;
}

}  // namespace

// =============================================================================
// Hit-test
// =============================================================================

GizmoHandle TransformGizmo::hitTest(const Viewport& viewport, EditorTool tool,
                                    const GizmoBounds& bounds, float screenX,
                                    float screenY) const
{
    if (!bounds.valid || dragging()) {
        return GizmoHandle::None;
    }
    // P4.1 (D2 — BUG DE UNIDADE DO P1 CORRIGIDO): screenDistanceTo
    // devolve PX DE TELA; o raio de acerto é comparado EM PX (hitPx ×
    // densidade) — constante no zoom. O código antigo convertia o raio
    // px→mundo (pxToWorld) e comparava 12px ≤ 0.5unidades: em zoom baixo
    // o alvo tinha ~4px (impossível de acertar — D3/D4) e em zoom alto
    // ~256px (pegava handle sem querer). Os testes antigos só passavam
    // porque tocavam no CENTRO EXATO dos handles.
    const float scale = viewport.uiScale();
    const float hitScreenPx = hitPx(scale);

    if (tool == EditorTool::Move) {
        // P4.1 (D1/D2): alvos em DUAS direções por eixo (±X, ±Y) — a
        // seta existe nos dois lados e o toque nela arrasta o EIXO.
        // Precisão: eixos primeiro (alvos menores), centro por último.
        const float axisLen = pxToWorld(viewport, axisPx(scale));
        if (screenDistanceTo(viewport, bounds.worldX + axisLen,
                             bounds.worldY, screenX, screenY) <= hitScreenPx ||
            screenDistanceTo(viewport, bounds.worldX - axisLen,
                             bounds.worldY, screenX, screenY) <= hitScreenPx) {
            return GizmoHandle::MoveAxisX;
        }
        if (screenDistanceTo(viewport, bounds.worldX,
                             bounds.worldY + axisLen, screenX,
                             screenY) <= hitScreenPx ||
            screenDistanceTo(viewport, bounds.worldX,
                             bounds.worldY - axisLen, screenX,
                             screenY) <= hitScreenPx) {
            return GizmoHandle::MoveAxisY;
        }
        if (screenDistanceTo(viewport, bounds.worldX, bounds.worldY,
                             screenX, screenY) <= hitScreenPx) {
            return GizmoHandle::MoveCenter;
        }
        return GizmoHandle::None;
    }

    if (tool == EditorTool::Rotate) {
        // P4.1 (D3): raio do anel com MÍNIMO de 64 px em tela — em zoom
        // baixo ou entidade pequena o anel continua agarrável.
        const float radiusPx =
            std::max(bounds.halfW, bounds.halfH) *
                viewport.effectiveCamera().zoom;
        const float radius =
            pxToWorld(viewport, ringRadiusPx(radiusPx, scale));
        const auto [hx, hy] = ringHandleWorld(bounds, radius);
        if (screenDistanceTo(viewport, hx, hy, screenX, screenY) <= hitScreenPx) {
            return GizmoHandle::RotateRing;
        }
        return GizmoHandle::None;
    }

    if (tool == EditorTool::Scale) {
        // Cantos primeiro (escala XY), depois arestas (um eixo) — P4.1.
        const CornerPoints corners = cornersOf(bounds);
        if (screenDistanceTo(viewport, corners.ne.first, corners.ne.second, screenX,
                             screenY) <= hitScreenPx) {
            return GizmoHandle::ScaleNE;
        }
        if (screenDistanceTo(viewport, corners.nw.first, corners.nw.second, screenX,
                             screenY) <= hitScreenPx) {
            return GizmoHandle::ScaleNW;
        }
        if (screenDistanceTo(viewport, corners.se.first, corners.se.second, screenX,
                             screenY) <= hitScreenPx) {
            return GizmoHandle::ScaleSE;
        }
        if (screenDistanceTo(viewport, corners.sw.first, corners.sw.second, screenX,
                             screenY) <= hitScreenPx) {
            return GizmoHandle::ScaleSW;
        }
        const EdgePoints edges = edgesOf(bounds);
        if (screenDistanceTo(viewport, edges.e.first, edges.e.second, screenX,
                             screenY) <= hitScreenPx) {
            return GizmoHandle::ScaleEdgeE;
        }
        if (screenDistanceTo(viewport, edges.w.first, edges.w.second, screenX,
                             screenY) <= hitScreenPx) {
            return GizmoHandle::ScaleEdgeW;
        }
        if (screenDistanceTo(viewport, edges.n.first, edges.n.second, screenX,
                             screenY) <= hitScreenPx) {
            return GizmoHandle::ScaleEdgeN;
        }
        if (screenDistanceTo(viewport, edges.s.first, edges.s.second, screenX,
                             screenY) <= hitScreenPx) {
            return GizmoHandle::ScaleEdgeS;
        }
        return GizmoHandle::None;
    }

    return GizmoHandle::None;  // Select: sem gizmo
}

// =============================================================================
// Drag
// =============================================================================

void TransformGizmo::beginDrag(GizmoHandle handle,
                               const GizmoTransform& startTransform,
                               const Viewport& viewport,
                               const GizmoBounds& bounds, float screenX,
                               float screenY)
{
    active_ = GizmoHandle::None;
    if (handle == GizmoHandle::None || !bounds.valid) {
        return;
    }
    active_ = handle;
    start_ = startTransform;

    const float worldX = viewport.screenToWorldX(screenX);
    const float worldY = viewport.screenToWorldY(screenY);
    grabWorldX_ = worldX;
    grabWorldY_ = worldY;

    if (handle == GizmoHandle::RotateRing) {
        startAngleRad_ = std::atan2(worldY - bounds.worldY,
                                    worldX - bounds.worldX);
    } else if (isCornerHandle(handle) || isEdgeHandle(handle)) {
        // Pointer no frame LOCAL do nó (desfaz a rotação) — o ratio
        // local é o que multiplica a escala (Godot-style: cantos
        // escalam X e Y; arestas P4.1 escalam UM eixo).
        const float c = std::cos(-bounds.rotation);
        const float s = std::sin(-bounds.rotation);
        const float dx = worldX - bounds.worldX;
        const float dy = worldY - bounds.worldY;
        startPointerLocalX_ = dx * c - dy * s;
        startPointerLocalY_ = dx * s + dy * c;
    }
}

GizmoTransform TransformGizmo::dragTo(const Viewport& viewport,
                                     const GizmoBounds& bounds, float screenX,
                                     float screenY) const
{
    if (active_ == GizmoHandle::None) {
        return start_;
    }
    GizmoTransform result = start_;
    const float worldX = viewport.screenToWorldX(screenX);
    const float worldY = viewport.screenToWorldY(screenY);
    const float dxWorld = worldX - grabWorldX_;
    const float dyWorld = worldY - grabWorldY_;

    switch (active_) {
    case GizmoHandle::MoveCenter:
        result.posX = start_.posX + dxWorld;
        result.posY = start_.posY + dyWorld;
        break;
    case GizmoHandle::MoveAxisX:
        result.posX = start_.posX + dxWorld;  // Y travado (P1.3)
        break;
    case GizmoHandle::MoveAxisY:
        result.posY = start_.posY + dyWorld;  // X travado
        break;
    case GizmoHandle::RotateRing: {
        const float angle = std::atan2(worldY - bounds.worldY,
                                       worldX - bounds.worldX);
        float deltaDeg = (angle - startAngleRad_) * 180.f / kPi;
        // Normaliza para (-180, 180]: a volta completa deve somar, não
        // teleportar (drag contínuo cruza ±180 várias vezes).
        while (deltaDeg > 180.f) { deltaDeg -= 360.f; }
        while (deltaDeg < -180.f) { deltaDeg += 360.f; }
        result.rotationDeg = snappedDegrees(start_.rotationDeg + deltaDeg);
        break;
    }
    case GizmoHandle::ScaleNE:
    case GizmoHandle::ScaleNW:
    case GizmoHandle::ScaleSE:
    case GizmoHandle::ScaleSW: {
        const float c = std::cos(-bounds.rotation);
        const float s = std::sin(-bounds.rotation);
        const float dx = worldX - bounds.worldX;
        const float dy = worldY - bounds.worldY;
        const float localX = dx * c - dy * s;
        const float localY = dx * s + dy * c;
        if (std::abs(startPointerLocalX_) > 1e-4f) {
            result.scaleX = clampScale(start_.scaleX * localX /
                                       startPointerLocalX_);
        }
        if (std::abs(startPointerLocalY_) > 1e-4f) {
            result.scaleY = clampScale(start_.scaleY * localY /
                                       startPointerLocalY_);
        }
        break;
    }
    case GizmoHandle::ScaleEdgeE:
    case GizmoHandle::ScaleEdgeW: {
        // P4.1 (D4): aresta E/W — escala SÓ no eixo X (o Y fica intacto,
        // distorção controlada pelo autor).
        const float c = std::cos(-bounds.rotation);
        const float s = std::sin(-bounds.rotation);
        const float dx = worldX - bounds.worldX;
        const float dy = worldY - bounds.worldY;
        const float localX = dx * c - dy * s;
        if (std::abs(startPointerLocalX_) > 1e-4f) {
            result.scaleX = clampScale(start_.scaleX * localX /
                                       startPointerLocalX_);
        }
        break;
    }
    case GizmoHandle::ScaleEdgeN:
    case GizmoHandle::ScaleEdgeS: {
        // P4.1 (D4): aresta N/S — escala SÓ no eixo Y.
        const float c = std::cos(-bounds.rotation);
        const float s = std::sin(-bounds.rotation);
        const float dx = worldX - bounds.worldX;
        const float dy = worldY - bounds.worldY;
        const float localY = dx * s + dy * c;
        if (std::abs(startPointerLocalY_) > 1e-4f) {
            result.scaleY = clampScale(start_.scaleY * localY /
                                       startPointerLocalY_);
        }
        break;
    }
    case GizmoHandle::None:
    default:
        break;
    }
    return result;
}

// =============================================================================
// Layout de desenho
// =============================================================================

std::vector<GizmoQuad> TransformGizmo::layoutQuads(const Viewport& viewport,
                                                   EditorTool tool,
                                                   const GizmoBounds& bounds) const
{
    std::vector<GizmoQuad> quads;
    if (!bounds.valid || tool == EditorTool::Select) {
        return quads;
    }
    const float scale = viewport.uiScale();
    const float handleHalf = pxToWorld(viewport, handlePx(scale)) * 0.5f;
    const float headHalf = pxToWorld(viewport, arrowHeadPx(scale)) * 0.5f;
    const float edgeHalf = pxToWorld(viewport, edgePx(scale)) * 0.5f;

    if (tool == EditorTool::Move) {
        // P4.1 (D1/D2): 4 SETAS (±X, ±Y) + quadrado central — affordance
        // completa; a ponta é maior que o handle (alvo óbvio).
        const float axisLen = pxToWorld(viewport, axisPx(scale));
        quads.push_back({bounds.worldX, bounds.worldY, handleHalf, handleHalf,
                         0.f, kCenterR, kCenterG, kCenterB});
        quads.push_back({bounds.worldX + axisLen, bounds.worldY, headHalf,
                         headHalf, 0.f, kXAxisR, kXAxisG, kXAxisB});
        quads.push_back({bounds.worldX - axisLen, bounds.worldY, headHalf,
                         headHalf, 0.f, kXAxisR, kXAxisG, kXAxisB});
        quads.push_back({bounds.worldX, bounds.worldY + axisLen, headHalf,
                         headHalf, 0.f, kYAxisR, kYAxisG, kYAxisB});
        quads.push_back({bounds.worldX, bounds.worldY - axisLen, headHalf,
                         headHalf, 0.f, kYAxisR, kYAxisG, kYAxisB});
        return quads;
    }

    if (tool == EditorTool::Rotate) {
        // P4.1 (D3): anel ≥64 px + handle dot ACoplado ao ângulo + ponta
        // de seta (o dot é o alvo, o spoke é a affordance).
        const float radiusPx =
            std::max(bounds.halfW, bounds.halfH) *
                viewport.effectiveCamera().zoom;
        const float radius =
            pxToWorld(viewport, ringRadiusPx(radiusPx, scale));
        const auto [hx, hy] = ringHandleWorld(bounds, radius);
        quads.push_back({hx, hy, headHalf, headHalf, 0.f, kRotateR,
                         kRotateG, kRotateB});
        return quads;
    }

    // Scale (P4.1/D4): 4 CANTOS + 4 MARCAS DE ARESTA na rotação da
    // entidade — cantos maiores (escala XY), arestas menores (1 eixo).
    const CornerPoints corners = cornersOf(bounds);
    quads.push_back({corners.ne.first, corners.ne.second, handleHalf, handleHalf,
                     bounds.rotation, kScaleR, kScaleG, kScaleB});
    quads.push_back({corners.nw.first, corners.nw.second, handleHalf, handleHalf,
                     bounds.rotation, kScaleR, kScaleG, kScaleB});
    quads.push_back({corners.se.first, corners.se.second, handleHalf, handleHalf,
                     bounds.rotation, kScaleR, kScaleG, kScaleB});
    quads.push_back({corners.sw.first, corners.sw.second, handleHalf, handleHalf,
                     bounds.rotation, kScaleR, kScaleG, kScaleB});
    const EdgePoints edges = edgesOf(bounds);
    quads.push_back({edges.e.first, edges.e.second, edgeHalf, edgeHalf,
                     bounds.rotation, kScaleR, kScaleG, kScaleB});
    quads.push_back({edges.w.first, edges.w.second, edgeHalf, edgeHalf,
                     bounds.rotation, kScaleR, kScaleG, kScaleB});
    quads.push_back({edges.n.first, edges.n.second, edgeHalf, edgeHalf,
                     bounds.rotation, kScaleR, kScaleG, kScaleB});
    quads.push_back({edges.s.first, edges.s.second, edgeHalf, edgeHalf,
                     bounds.rotation, kScaleR, kScaleG, kScaleB});
    return quads;
}

std::vector<GizmoSegment> TransformGizmo::layoutSegments(
    const Viewport& viewport, EditorTool tool,
    const GizmoBounds& bounds) const
{
    std::vector<GizmoSegment> segments;
    if (!bounds.valid || tool == EditorTool::Select) {
        return segments;
    }
    const float scale = viewport.uiScale();

    if (tool == EditorTool::Move) {
        // P4.1: hastes das 4 setas (partem da borda do bounds — não
        // cobrem a arte da entidade).
        const float axisLen = pxToWorld(viewport, axisPx(scale));
        const float startX = std::max(bounds.halfW, 0.f);
        const float startY = std::max(bounds.halfH, 0.f);
        segments.push_back({bounds.worldX + startX, bounds.worldY,
                            bounds.worldX + axisLen, bounds.worldY,
                            kXAxisR, kXAxisG, kXAxisB});
        segments.push_back({bounds.worldX - startX, bounds.worldY,
                            bounds.worldX - axisLen, bounds.worldY,
                            kXAxisR, kXAxisG, kXAxisB});
        segments.push_back({bounds.worldX, bounds.worldY + startY,
                            bounds.worldX, bounds.worldY + axisLen,
                            kYAxisR, kYAxisG, kYAxisB});
        segments.push_back({bounds.worldX, bounds.worldY - startY,
                            bounds.worldX, bounds.worldY - axisLen,
                            kYAxisR, kYAxisG, kYAxisB});
        return segments;
    }

    if (tool == EditorTool::Rotate) {
        // P4.1 (D3): anel 32 lados + SPOKE do centro ao handle + PONTA
        // DE SETA (duas tangentes curtas no handle — chevron). O autor
        // VÊ de onde girar.
        const float radiusPx =
            std::max(bounds.halfW, bounds.halfH) *
                viewport.effectiveCamera().zoom;
        const float radius =
            pxToWorld(viewport, ringRadiusPx(radiusPx, scale));
        constexpr int kSides = 32;  // suave em zoom alto
        float prevX = bounds.worldX + radius * std::cos(bounds.rotation);
        float prevY = bounds.worldY + radius * std::sin(bounds.rotation);
        for (int i = 1; i <= kSides; ++i) {
            const float angle = bounds.rotation +
                                (2.f * kPi * static_cast<float>(i)) /
                                    static_cast<float>(kSides);
            const float nextX = bounds.worldX + radius * std::cos(angle);
            const float nextY = bounds.worldY + radius * std::sin(angle);
            segments.push_back({prevX, prevY, nextX, nextY, kRotateR,
                                kRotateG, kRotateB});
            prevX = nextX;
            prevY = nextY;
        }
        // Spoke radial (centro → handle).
        const auto [hx, hy] = ringHandleWorld(bounds, radius);
        segments.push_back({bounds.worldX, bounds.worldY, hx, hy, kRotateR,
                            kRotateG, kRotateB});
        // Chevron (ponta de seta tangente no handle — 20% do raio).
        const float chev = radius * 0.2f;
        const float cx = std::cos(bounds.rotation + kPi * 0.5f);
        const float sy = std::sin(bounds.rotation + kPi * 0.5f);
        const float rx = std::cos(bounds.rotation);
        const float ry = std::sin(bounds.rotation);
        // Tangentes ± chevron: do handle, para trás e para os lados.
        segments.push_back({hx - rx * chev + cx * chev,
                            hy - ry * chev + sy * chev,
                            hx, hy, kRotateR, kRotateG, kRotateB});
        segments.push_back({hx - rx * chev - cx * chev,
                            hy - ry * chev - sy * chev,
                            hx, hy, kRotateR, kRotateG, kRotateB});
        return segments;
    }

    // Scale (P4.1/D4): diagonais do centro aos CANTOS (guia) + arestas
    // do retângulo (o quad que o autor está escalando — os handles de
    // aresta ganham sentido visual).
    const CornerPoints corners = cornersOf(bounds);
    segments.push_back({bounds.worldX, bounds.worldY, corners.ne.first, corners.ne.second,
                        kScaleR, kScaleG, kScaleB});
    segments.push_back({bounds.worldX, bounds.worldY, corners.nw.first, corners.nw.second,
                        kScaleR, kScaleG, kScaleB});
    segments.push_back({bounds.worldX, bounds.worldY, corners.se.first, corners.se.second,
                        kScaleR, kScaleG, kScaleB});
    segments.push_back({bounds.worldX, bounds.worldY, corners.sw.first, corners.sw.second,
                        kScaleR, kScaleG, kScaleB});
    const EdgePoints edges = edgesOf(bounds);
    segments.push_back({corners.nw.first, corners.nw.second, edges.n.first,
                        edges.n.second, kScaleR, kScaleG, kScaleB});
    segments.push_back({edges.n.first, edges.n.second, corners.ne.first,
                        corners.ne.second, kScaleR, kScaleG, kScaleB});
    segments.push_back({corners.sw.first, corners.sw.second, edges.s.first,
                        edges.s.second, kScaleR, kScaleG, kScaleB});
    segments.push_back({edges.s.first, edges.s.second, corners.se.first,
                        corners.se.second, kScaleR, kScaleG, kScaleB});
    segments.push_back({corners.nw.first, corners.nw.second, edges.w.first,
                        edges.w.second, kScaleR, kScaleG, kScaleB});
    segments.push_back({edges.w.first, edges.w.second, corners.sw.first,
                        corners.sw.second, kScaleR, kScaleG, kScaleB});
    segments.push_back({corners.ne.first, corners.ne.second, edges.e.first,
                        edges.e.second, kScaleR, kScaleG, kScaleB});
    segments.push_back({edges.e.first, edges.e.second, corners.se.first,
                        corners.se.second, kScaleR, kScaleG, kScaleB});
    return segments;
}

}  // namespace eng::editor
