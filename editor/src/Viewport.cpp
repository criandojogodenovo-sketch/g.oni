#include "eng/editor/Viewport.hpp"

/// Viewport — câmera 2D, conversões, quads e hit-test (FASE 8).
///
/// Ver header para o modelo de coordenadas e decisões (auditoria D3).

#include <algorithm>
#include <cmath>

#include "eng/editor/SpriteData.hpp"
#include "eng/particles/Particles.hpp"
#include "eng/physics/Physics.hpp"
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
// Conversões tela ↔ mundo (P0-5: passam pela câmera EM FOCO — a de jogo
// quando ativa em Play, senão a do editor — ADR-051)
// =============================================================================

float Viewport::worldToScreenX(float wx) const noexcept
{
    const Camera2D& camera = effectiveCamera();
    return screenW_ * 0.5f + (wx - camera.posX) * camera.zoom;
}

float Viewport::worldToScreenY(float wy) const noexcept
{
    const Camera2D& camera = effectiveCamera();
    return screenH_ * 0.5f - (wy - camera.posY) * camera.zoom;
}

float Viewport::screenToWorldX(float sx) const noexcept
{
    const Camera2D& camera = effectiveCamera();
    return (sx - screenW_ * 0.5f) / camera.zoom + camera.posX;
}

float Viewport::screenToWorldY(float sy) const noexcept
{
    const Camera2D& camera = effectiveCamera();
    return camera.posY - (sy - screenH_ * 0.5f) / camera.zoom;
}

void Viewport::pan(float screenDx, float screenDy) noexcept
{
    if (gameCameraActive()) {
        return; // ADR-051: a câmera em foco é do JOGO — pan é no-op
    }
    camera_.posX -= screenDx / camera_.zoom;
    camera_.posY += screenDy / camera_.zoom;
}

void Viewport::zoomAt(float factor, float screenFocusX, float screenFocusY) noexcept
{
    if (gameCameraActive()) {
        return; // ADR-051: zoom é no-op sob câmera de jogo
    }
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
        // Camadas (P0-5, ADR-051): entidades em camada sem participação
        // de render NÃO geram quad (filhos continuam sendo visitados — a
        // camada é por entidade, não herdada).
        if (!scene.participatesIn(node, eng::scene::LayerStage::Render)) {
            scene.eachChild(node, [&](eng::ecs::Entity child) {
                self(self, child, depth + 1);
            });
            return;
        }
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

        // Sprite (evolução P0-3): SpriteData REAL substitui o marcador hue.
        // Tamanho do sprite em mundo = escala local × (região em PIXELS do
        // arquivo / pixelsPerUnit) — os pixels da textura vêm do cache do
        // HOST (aqui é camada de dados, sem renderer); sem metadados, a
        // região UV × 1 unidade de mundo serve de estimativa e o renderer
        // corrige na escala final.
        if (const auto* sprite = scene.world().get<eng::editor::SpriteData>(node)) {
            quad.textureAsset = sprite->textureAsset;
            quad.u0 = sprite->u0;
            quad.v0 = sprite->v0;
            quad.u1 = sprite->u1;
            quad.v1 = sprite->v1;
            quad.tintR = sprite->tintR;
            quad.tintG = sprite->tintG;
            quad.tintB = sprite->tintB;
            quad.tintA = sprite->opacity;
            quad.flipX = sprite->flipX;
            quad.flipY = sprite->flipY;
            quad.sort = sprite->sort;
            quad.spritePpu = sprite->pixelsPerUnit > 0.f ? sprite->pixelsPerUnit
                                                         : 1.f;
            quad.pivotX = sprite->pivotX;
            quad.pivotY = sprite->pivotY;
        }

        // Collider (RECOVERY §10): geometria nas MESMAS convenções do
        // PhysicsWorld::worldShapeOf — centrado no nó, raio/halfExtents
        // escalados pela norma das colunas do world matrix (sizeX/sizeY
        // do quad JÁ são essas normas). O runtime usa exatamente isto;
        // agora o autor VÊ o mesmo shape que a física usará.
        if (const auto* collider =
                scene.world().get<eng::physics::Collider>(node)) {
            quad.hasCollider = true;
            quad.colliderIsSphere =
                collider->shape == eng::physics::ColliderShape::Sphere;
            quad.colliderTrigger = collider->isTrigger;
            if (quad.colliderIsSphere) {
                quad.colliderHalfX = collider->radius * quad.sizeX;
                quad.colliderHalfY = quad.colliderHalfX;
            } else {
                quad.colliderHalfX = collider->halfExtents.x * quad.sizeX;
                quad.colliderHalfY = collider->halfExtents.y * quad.sizeY;
            }
        }
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

std::vector<ParticleQuad> Viewport::buildParticleQuads(
    const eng::scene::Scene& scene) const
{
    // Auditoria final (drift D6 da FASE 10): o viewport prometia desenhar
    // partículas como quads — nada lia a ParticlePool. Uma por partícula
    // VIVA (pool é runtime-only; em Play o clone tem as pools ativas).
    // Camadas (P0-5, ADR-051): pool de camada sem render NÃO desenha.
    std::vector<ParticleQuad> quads;
    scene.world().each<eng::particles::ParticlePool>(
        [&](eng::ecs::Entity emitter,
            const eng::particles::ParticlePool& pool) {
            if (!scene.participatesIn(emitter,
                                      eng::scene::LayerStage::Render)) {
                return;
            }
            quads.reserve(quads.size() + pool.particles.size());
            for (const auto& particle : pool.particles) {
                ParticleQuad quad;
                quad.worldX = particle.position.x;
                quad.worldY = particle.position.y;
                quad.size = particle.size;
                quad.rotation = particle.rotation;
                quads.push_back(quad);
            }
        });
    return quads;
}

std::optional<eng::ecs::Entity> Viewport::hitTest(
    const std::vector<EntityQuad>& quads, float screenX, float screenY,
    float touchRadius) const noexcept
{
    // Top-most = último desenhado (frente). Raio generoso p/ dedo (§8.8).
    // P0-5: usa a câmera EM FOCO (de jogo quando ativa) — o toque segue
    // a câmera que o usuário está vendo.
    const Camera2D& camera = effectiveCamera();
    for (auto it = quads.rbegin(); it != quads.rend(); ++it) {
        const float centerSX = worldToScreenX(it->worldX);
        const float centerSY = worldToScreenY(it->worldY);
        const float halfPX =
            std::max(it->sizeX * camera.zoom * 0.5f, kMinQuadPixels * 0.5f);
        const float halfPY =
            std::max(it->sizeY * camera.zoom * 0.5f, kMinQuadPixels * 0.5f);
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
