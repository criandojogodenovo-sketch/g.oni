#include "eng/editor/Viewport.hpp"

/// Viewport — câmera 2D, conversões, quads e hit-test (FASE 8).
///
/// Ver header para o modelo de coordenadas e decisões (auditoria D3).

#include <algorithm>
#include <cmath>

#include "eng/scene/Name.hpp"

namespace eng::editor {

namespace {

/// Hue determinístico por entidade (index) — visualmente distinguível sem
/// semântica falsa (missão §8.6: marcador, não classificador).
[[nodiscard]] std::uint32_t hueOf(eng::ecs::Entity entity) noexcept
{
    return (entity.index * 47u + 13u) % 360u;
}

}  // namespace

// =============================================================================
// Conversões tela ↔ mundo
// =============================================================================

float Viewport::worldToScreenX(float wx) const noexcept
{
    return screenW_ * 0.5f + (wx - camera_.posX) * camera_.zoom;
}

float Viewport::worldToScreenY(float wy) const noexcept
{
    return screenH_ * 0.5f - (wy - camera_.posY) * camera_.zoom;
}

float Viewport::screenToWorldX(float sx) const noexcept
{
    return (sx - screenW_ * 0.5f) / camera_.zoom + camera_.posX;
}

float Viewport::screenToWorldY(float sy) const noexcept
{
    return camera_.posY - (sy - screenH_ * 0.5f) / camera_.zoom;
}

void Viewport::pan(float screenDx, float screenDy) noexcept
{
    camera_.posX -= screenDx / camera_.zoom;
    camera_.posY += screenDy / camera_.zoom;
}

void Viewport::zoomAt(float factor, float screenFocusX, float screenFocusY) noexcept
{
    if (factor <= 0.f || !std::isfinite(factor)) {
        return;
    }
    // Mundo sob o foco ANTES do zoom — ele permanece fixo na tela.
    const float focusWX = screenToWorldX(screenFocusX);
    const float focusWY = screenToWorldY(screenFocusY);

    camera_.zoom = std::clamp(camera_.zoom * factor, kMinZoom, kMaxZoom);

    camera_.posX = focusWX - (screenFocusX - screenW_ * 0.5f) / camera_.zoom;
    camera_.posY = focusWY + (screenFocusY - screenH_ * 0.5f) / camera_.zoom;
}

void Viewport::setScreenSize(float width, float height) noexcept
{
    screenW_ = width > 0.f && std::isfinite(width) ? width : 1.f;
    screenH_ = height > 0.f && std::isfinite(height) ? height : 1.f;
}

// =============================================================================
// Quads e hit-test
// =============================================================================

std::vector<EntityQuad> Viewport::buildQuads(
    const eng::scene::Scene& scene,
    const std::optional<eng::ecs::Entity>& selection) const
{
    std::vector<EntityQuad> quads;
    quads.reserve(scene.nodeCount());

    // Caminhada depth-first estável (mesma ordem do hierarchySnapshot — a
    // ORDEM DE DESENHO; hit-test varre de trás para frente).
    const auto visit = [&](auto&& self, eng::ecs::Entity node, int depth) -> void {
        const eng::math::Mat4 world = scene.computeWorldMatrix(node);

        EntityQuad quad;
        quad.entity = node;
        // Column-major: translação vive na 4ª COLUNA — at(3, row).
        quad.worldX = world.at(3, 0);
        quad.worldY = world.at(3, 1);
        // Escala = comprimento das colunas da base (ADR-025/TRS).
        quad.sizeX = std::sqrt(world.at(0, 0) * world.at(0, 0) +
                              world.at(0, 1) * world.at(0, 1) +
                              world.at(0, 2) * world.at(0, 2));
        quad.sizeY = std::sqrt(world.at(1, 0) * world.at(1, 0) +
                              world.at(1, 1) * world.at(1, 1) +
                              world.at(1, 2) * world.at(1, 2));
        quad.rotation = std::atan2(world.at(0, 1), world.at(0, 0));
        quad.tint = hueOf(node);
        quad.selected = selection.has_value() && *selection == node;
        quads.push_back(quad);
        (void)depth; // profundidade não muda o quad — reserva de API futura

        scene.eachChild(node, [&](eng::ecs::Entity child) {
            self(self, child, depth + 1);
        });
    };

    // Raízes: iteração sobre filhos "da cena" (pai = kNoEntity) — World não
    // expõe enumeração de pools; percorre via Hierarchy? Não: a Scene não
    // lista raízes diretamente — usa-se o world com cada nó criado. Solução:
    // percorrer TODOS os nós vivos e filtrar raízes (pai == kNoEntity) em
    // ordem estável por índice.
    std::vector<eng::ecs::Entity> roots;
    scene.world().each<eng::scene::Hierarchy>(
        [&](eng::ecs::Entity node, const eng::scene::Hierarchy& hierarchy) {
            if (hierarchy.parent == eng::scene::kNoEntity && scene.isNode(node)) {
                roots.push_back(node);
            }
        });
    std::sort(roots.begin(), roots.end(),
              [](eng::ecs::Entity a, eng::ecs::Entity b) {
                  return a.index < b.index;
              });
    for (const eng::ecs::Entity root : roots) {
        visit(visit, root, 0);
    }
    return quads;
}

std::optional<eng::ecs::Entity> Viewport::hitTest(
    const std::vector<EntityQuad>& quads, float screenX, float screenY,
    float touchRadius) const noexcept
{
    // Top-most = último desenhado (frente). Raio generoso p/ dedo (§8.8).
    for (auto it = quads.rbegin(); it != quads.rend(); ++it) {
        const float centerSX = worldToScreenX(it->worldX);
        const float centerSY = worldToScreenY(it->worldY);
        const float halfPX =
            std::max(it->sizeX * camera_.zoom * 0.5f, kMinQuadPixels * 0.5f);
        const float halfPY =
            std::max(it->sizeY * camera_.zoom * 0.5f, kMinQuadPixels * 0.5f);
        const float dx = std::abs(screenX - centerSX);
        const float dy = std::abs(screenY - centerSY);
        if (dx <= halfPX + touchRadius && dy <= halfPY + touchRadius) {
            return it->entity;
        }
    }
    return std::nullopt;
}

std::size_t Viewport::quadCount(const std::vector<EntityQuad>& quads) const noexcept
{
    return quads.size();
}

}  // namespace eng::editor
