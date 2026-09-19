#include "eng/tick/Camera.hpp"

/// CameraData/resolveActiveCamera/CameraTickSystem — implementação
/// (evolução P0-5; ADR-051).

#include "eng/log/Macros.hpp"
#include "eng/math/Mat4.hpp"

ENG_LOG_CATEGORY("tick");

namespace eng::tick {

ActiveCamera resolveActiveCamera(const eng::scene::Scene& scene)
{
    // cada<CameraData> itera o pool na ORDEM DE INSERÇÃO (ADR-024) —
    // índice de criação = ordem estável e determinística. Dados COPIADOS
    // por valor (ActiveCamera não retém ponteiro de pool — ver Camera.hpp).
    //
    // P2 (§11 — "o runtime usa a câmera real"): posX/posY são OFFSETS a
    // partir da posição-MUNDO da ENTIDADE (a câmera é acoplada ao
    // Transform — mover a entidade no editor move a vista no Play).
    // Entidades na origem (todas as cenas pré-P2) preservam o
    // comportamento absoluto antigo: offset 0 + entidade em (0,0).
    ActiveCamera resolved;
    scene.world().each<CameraData>(
        [&](eng::ecs::Entity e, const CameraData& camera) {
            if (!resolved.has && camera.active) {
                resolved.entity = e;
                resolved.data = camera;
                const eng::math::Mat4 world = scene.computeWorldMatrix(e);
                resolved.data.posX += world.at(3, 0);
                resolved.data.posY += world.at(3, 1);
                resolved.has = true;
            }
        });
    return resolved;
}

std::size_t activeCameraCount(const eng::scene::Scene& scene)
{
    std::size_t count = 0;
    scene.world().each<CameraData>(
        [&](eng::ecs::Entity /*e*/, const CameraData& camera) {
            if (camera.active) {
                ++count;
            }
        });
    return count;
}

void CameraTickSystem::tick(eng::scene::Scene& scene, float /*dt*/)
{
    active_ = resolveActiveCamera(scene);
    const std::size_t actives = activeCameraCount(scene);
    if (actives > 1) {
        ENG_WARN(
            "CameraTick: {} câmeras ativas na cena — a primeira em ordem "
            "de criação vence (desative as demais)",
            actives);
    }
}

}  // namespace eng::tick
