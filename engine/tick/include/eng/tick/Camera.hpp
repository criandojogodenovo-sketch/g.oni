#pragma once

/// eng::tick::CameraData — câmera de jogo 2D como CIDADÃ DA CENA
/// (evolução P0-5; ADR-051 — "CameraTick").
///
/// - Antes da P0-5 a câmera era estado do EDITOR (Viewport::Camera2D) —
///   um jogo exportado não tinha como definir a própria câmera. Agora a
///   câmera é um componente: o entity system resolve a ATIVA e o viewport
///   do editor a SEGUE em Play (hit-test e arraste continuam corretos —
///   todas as conversões world↔screen passam pela câmera em foco).
/// - Ortográfica 2D: posX/posY em unidades de mundo, zoom em
///   PIXELS POR UNIDADE (mesma semântica da câmera do editor — conversão
///   direta). Rotação/perspectiva ficam para o rework de câmera 3D (P1):
///   campo sem consumidor seria API inerte (missão proíbe).
/// - Várias câmeras: a PRIMEIRA ativa em ordem estável (índice de criação)
///   vence; o CameraTickSystem AVISA ambiguidade (dados não ficam em
///   silêncio). `active=false` desliga sem remover.

#include "eng/ecs/Ecs.hpp"
#include "eng/scene/Scene.hpp"
#include "eng/tick/Tick.hpp"

namespace eng::tick {

/// Componente câmera de jogo (refletido → Inspector + persistência +
/// clone no Play — ADR-043).
struct CameraData {
    float posX{0.f};
    float posY{0.f};
    /// Pixels por unidade de mundo (idem Viewport::Camera2D::zoom).
    float zoom{48.f};
    /// Câmera desligada sem remover (a resolução ignora).
    bool active{true};
};

/// Câmera ativa resolvida — ou nenhuma. Dados POR VALOR: ponteiros de
/// pool não sobrevivem a emplaces do mesmo tipo (dense array realoca,
/// ADR-024) — reter `const CameraData*` entre mutações da cena seria
/// armadilha de use-after-free.
struct ActiveCamera {
    eng::ecs::Entity entity{};
    CameraData data{};

    /// Havia uma câmera ativa? (o default de CameraData não responde
    /// isso — `has` é explícito.)
    bool has = false;

    [[nodiscard]] bool found() const noexcept { return has; }
};

/// Resolve a câmera ATIVA da cena: primeira CameraData ativa em ordem
/// estável (índice de criação — cada<CameraData> itera o pool na ordem de
/// inserção, ADR-024). Determinística. Sem câmera → {.has = false}.
[[nodiscard]] ActiveCamera resolveActiveCamera(
    const eng::scene::Scene& scene);

/// Contagem de câmeras ATIVAS (diagnóstico de ambiguidade — o tick avisa
/// quando > 1).
[[nodiscard]] std::size_t activeCameraCount(
    const eng::scene::Scene& scene);

/// CameraTick (fase PreRender): cacheia a câmera ativa do frame e AVISA
/// ambiguidade. Consumidores com scheduler consultam o cache; sem
/// scheduler, `resolveActiveCamera` direto.
class CameraTickSystem final : public TickSystem {
public:
    [[nodiscard]] const char* name() const override { return "CameraTick"; }
    [[nodiscard]] Phase phase() const override { return Phase::PreRender; }

    void tick(eng::scene::Scene& scene, float /*dt*/) override;

    /// Câmera ativa do ÚLTIMO frame (antes do primeiro tick: vazia).
    [[nodiscard]] const ActiveCamera& activeCamera() const noexcept
    {
        return active_;
    }

private:
    ActiveCamera active_{};
};

}  // namespace eng::tick

/// Reflexão (ADR-043: campos por caminho — Inspector/serializer).
ENG_REFLECT_BEGIN(eng::tick::CameraData)
    ENG_REFLECT_FIELD(posX)
    ENG_REFLECT_FIELD(posY)
    ENG_REFLECT_FIELD(zoom)
    ENG_REFLECT_FIELD(active)
ENG_REFLECT_END()
