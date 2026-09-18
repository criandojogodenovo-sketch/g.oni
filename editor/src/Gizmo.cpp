#include "eng/editor/Gizmo.hpp"

/// TransformGizmo — implementação (P1).
///
/// Hit-test + matemática de drag + layout de desenho. Ver header para
/// as decisões (handles constantes em tela; drag devolve estado ALVO;
/// bounds = tamanho desenhado da entidade).

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
    const float hitWorld = pxToWorld(viewport, kHitPx);

    if (tool == EditorTool::Move) {
        // Precisão: eixos primeiro (alvos menores), centro por último.
        const float axisLen = pxToWorld(viewport, kAxisPx);
        const float axisHalfW = std::max(bounds.halfW, 0.f);
        // Alvo do eixo X = quadrado no fim da seta (+X).
        if (screenDistanceTo(viewport, bounds.worldX + axisLen,
                             bounds.worldY, screenX, screenY) <= hitWorld) {
            return GizmoHandle::MoveAxisX;
        }
        if (screenDistanceTo(viewport, bounds.worldX,
                             bounds.worldY + axisLen, screenX,
                             screenY) <= hitWorld) {
            return GizmoHandle::MoveAxisY;
        }
        if (screenDistanceTo(viewport, bounds.worldX, bounds.worldY,
                             screenX, screenY) <= hitWorld) {
            return GizmoHandle::MoveCenter;
        }
        (void)axisHalfW;
        return GizmoHandle::None;
    }

    if (tool == EditorTool::Rotate) {
        const float radius =
            std::max(bounds.halfW, bounds.halfH) +
            pxToWorld(viewport, kRingPadPx);
        const auto [hx, hy] = ringHandleWorld(bounds, radius);
        if (screenDistanceTo(viewport, hx, hy, screenX, screenY) <= hitWorld) {
            return GizmoHandle::RotateRing;
        }
        return GizmoHandle::None;
    }

    if (tool == EditorTool::Scale) {
        const CornerPoints corners = cornersOf(bounds);
        if (screenDistanceTo(viewport, corners.ne.first, corners.ne.second, screenX,
                             screenY) <= hitWorld) {
            return GizmoHandle::ScaleNE;
        }
        if (screenDistanceTo(viewport, corners.nw.first, corners.nw.second, screenX,
                             screenY) <= hitWorld) {
            return GizmoHandle::ScaleNW;
        }
        if (screenDistanceTo(viewport, corners.se.first, corners.se.second, screenX,
                             screenY) <= hitWorld) {
            return GizmoHandle::ScaleSE;
        }
        if (screenDistanceTo(viewport, corners.sw.first, corners.sw.second, screenX,
                             screenY) <= hitWorld) {
            return GizmoHandle::ScaleSW;
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
    } else if (handle == GizmoHandle::ScaleNE || handle == GizmoHandle::ScaleNW ||
               handle == GizmoHandle::ScaleSE ||
               handle == GizmoHandle::ScaleSW) {
        // Pointer no frame LOCAL do nó (desfaz a rotação) — o ratio
        // local é o que multiplica a escala (Godot-style: cada canto
        // escala X e Y independentemente).
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
    const float handleHalf = pxToWorld(viewport, kHandlePx) * 0.5f;

    if (tool == EditorTool::Move) {
        const float axisLen = pxToWorld(viewport, kAxisPx);
        quads.push_back({bounds.worldX, bounds.worldY, handleHalf, handleHalf,
                         0.f, kCenterR, kCenterG, kCenterB});
        quads.push_back({bounds.worldX + axisLen, bounds.worldY, handleHalf,
                         handleHalf, 0.f, kXAxisR, kXAxisG, kXAxisB});
        quads.push_back({bounds.worldX, bounds.worldY + axisLen, handleHalf,
                         handleHalf, 0.f, kYAxisR, kYAxisG, kYAxisB});
        return quads;
    }

    if (tool == EditorTool::Rotate) {
        const float radius = std::max(bounds.halfW, bounds.halfH) +
                             pxToWorld(viewport, kRingPadPx);
        const auto [hx, hy] = ringHandleWorld(bounds, radius);
        quads.push_back({hx, hy, handleHalf, handleHalf, 0.f, kRotateR,
                         kRotateG, kRotateB});
        return quads;
    }

    // Scale: 4 cantos na ROTAÇÃO da entidade (seguem o bounds real).
    const CornerPoints corners = cornersOf(bounds);
    quads.push_back({corners.ne.first, corners.ne.second, handleHalf, handleHalf,
                     bounds.rotation, kScaleR, kScaleG, kScaleB});
    quads.push_back({corners.nw.first, corners.nw.second, handleHalf, handleHalf,
                     bounds.rotation, kScaleR, kScaleG, kScaleB});
    quads.push_back({corners.se.first, corners.se.second, handleHalf, handleHalf,
                     bounds.rotation, kScaleR, kScaleG, kScaleB});
    quads.push_back({corners.sw.first, corners.sw.second, handleHalf, handleHalf,
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

    if (tool == EditorTool::Move) {
        const float axisLen = pxToWorld(viewport, kAxisPx);
        // Eixos partem da borda do bounds (não do centro) para não
        // cobrir a arte da entidade.
        const float startX = bounds.worldX + std::max(bounds.halfW, 0.f);
        const float startY = bounds.worldY + std::max(bounds.halfH, 0.f);
        segments.push_back({startX, bounds.worldY,
                            bounds.worldX + axisLen, bounds.worldY,
                            kXAxisR, kXAxisG, kXAxisB});
        segments.push_back({bounds.worldX, startY, bounds.worldX,
                            bounds.worldY + axisLen, kYAxisR, kYAxisG,
                            kYAxisB});
        return segments;
    }

    if (tool == EditorTool::Rotate) {
        // Anel octogonal na ROTAÇÃO do nó (o handle acoplado ao ângulo).
        const float radius = std::max(bounds.halfW, bounds.halfH) +
                             pxToWorld(viewport, kRingPadPx);
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
        return segments;
    }

    // Scale: diagonais do centro aos cantos (guia visual).
    const CornerPoints corners = cornersOf(bounds);
    segments.push_back({bounds.worldX, bounds.worldY, corners.ne.first, corners.ne.second,
                        kScaleR, kScaleG, kScaleB});
    segments.push_back({bounds.worldX, bounds.worldY, corners.nw.first, corners.nw.second,
                        kScaleR, kScaleG, kScaleB});
    segments.push_back({bounds.worldX, bounds.worldY, corners.se.first, corners.se.second,
                        kScaleR, kScaleG, kScaleB});
    segments.push_back({bounds.worldX, bounds.worldY, corners.sw.first, corners.sw.second,
                        kScaleR, kScaleG, kScaleB});
    return segments;
}

}  // namespace eng::editor
