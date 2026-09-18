#include "eng/tick/Camera.hpp"

/// CameraData/resolveActiveCamera/CameraTickSystem — implementação
/// (evolução P0-5; ADR-051).

#include "eng/log/Macros.hpp"

ENG_LOG_CATEGORY("tick");

namespace eng::tick {

ActiveCamera resolveActiveCamera(const eng::scene::Scene& scene)
{
    // cada<CameraData> itera o pool na ORDEM DE INSERÇÃO (ADR-024) —
    // índice de criação = ordem estável e determinística. Dados COPIADOS
    // por valor (ActiveCamera não retém ponteiro de pool — ver Camera.hpp).
    ActiveCamera resolved;
    scene.world().each<CameraData>(
        [&](eng::ecs::Entity e, const CameraData& camera) {
            if (!resolved.has && camera.active) {
                resolved.entity = e;
                resolved.data = camera;
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
