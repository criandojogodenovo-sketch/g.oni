#pragma once

/// eng::animation — clips, animator e máquina de estados (FASE 10, §7.7–§7.11).
///
/// - Clips com keyframes TRS (position lerp, rotation SLERP — §7.8);
/// - Animator é COMPONENTE: play/pause/stop/loop/speed/seek (§7.9);
/// - Estados + transições com cross-fade linear (§7.10);
/// - SKELETAL (§7.11): a hierarquia de nós da cena É a preparação (pose =
///   transforms de nós); skinning/mesh é EXTENSÃO FUTURA documentada —
///   ainda não há mesh renderer no engine.
/// - Sem RHI/Android; aplica TRS no Transform do próprio nó.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "eng/math/Quat.hpp"
#include "eng/math/Vec3.hpp"
#include "eng/reflect/Reflect.hpp"
#include "eng/scene/Scene.hpp"

namespace eng::animation {

// =============================================================================
// Keyframes (§7.8)
// =============================================================================

template <typename T>
struct Keyframe {
    using value_type = T;
    float time{0.f};
    T value{};
};

using PositionKey = Keyframe<eng::math::Vec3>;
using RotationKey = Keyframe<eng::math::Quat>;
using ScaleKey = Keyframe<eng::math::Vec3>;

/// Um clip de animação de TRANSFORM (§7.7/§7.8).
struct AnimationClip {
    std::string name;
    std::vector<PositionKey> position;
    std::vector<RotationKey> rotation;
    std::vector<ScaleKey> scale;

    /// Duração = último keyframe de qualquer track.
    [[nodiscard]] float duration() const noexcept;
};

/// Biblioteca de clips por nome (banco do runtime — §9: API C++).
class AnimationBank final {
public:
    void add(AnimationClip clip);
    [[nodiscard]] const AnimationClip* find(std::string_view name) const
        noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return clips_.size(); }

private:
    std::unordered_map<std::string, AnimationClip> clips_;
};

// =============================================================================
// Animator (§7.9) — componente ECS, refletido/serializável
// =============================================================================

struct Animator {
    std::string clip{"idle"};   ///< clip corrente (por nome do banco)
    float time{0.f};            ///< cursor de playback (segundos)
    float speed{1.f};          ///< 0.5 = metade, 2 = dobro
    bool loop{true};
    bool playing{false};
    bool applyPosition{true};
    bool applyRotation{true};
    bool applyScale{true};
};

ENG_REFLECT_BEGIN(eng::animation::Animator)
    ENG_REFLECT_FIELD(clip)
    ENG_REFLECT_FIELD(time)
    ENG_REFLECT_FIELD(speed)
    ENG_REFLECT_FIELD(loop)
    ENG_REFLECT_FIELD(playing)
    ENG_REFLECT_FIELD(applyPosition)
    ENG_REFLECT_FIELD(applyRotation)
    ENG_REFLECT_FIELD(applyScale)
ENG_REFLECT_END()

// =============================================================================
// Estados e transições (§7.10)
// =============================================================================

struct AnimationTransition {
    std::string from;
    std::string to;
    float blendDuration{0.15f};
};

/// Estado do Animator além do clip: transição ativa (cross-fade).
struct AnimatorState {
    std::string current;
    std::string previous;
    float blendRemaining{0.f};
    float blendDuration{0.f};
    float previousTime{0.f};
};

/// Máquina de estados mínima (§7.10): Idle→Run→Jump→Attack são NOMES —
/// as REGRAS ficam no gameplay (C++/script — §9); a engine fornece a
/// transição com cross-fade.
class AnimatorStateMachine final {
public:
    explicit AnimatorStateMachine(Animator& animator) noexcept
        : animator_(animator)
    {
    }

    /// Dispara uma transição para o estado/clip `next` (trocando o clip do
    /// Animator e iniciando o cross-fade com o anterior). Transição para o
    /// estado atual: no-op.
    void transition(const AnimationBank& bank, std::string_view next,
                    float blendDuration = 0.15f);

    /// Avança o estado (blend) e o playback do Animator.
    void update(const AnimationBank& bank, float deltaSeconds);

    [[nodiscard]] const AnimatorState& state() const noexcept
    {
        return state_;
    }

private:
    Animator& animator_;
    AnimatorState state_{};
};

// =============================================================================
// Sistema — aplica TRS interpolado (§7.8/§7.9)
// =============================================================================

class AnimationSystem final {
public:
    AnimationSystem() = delete;

    /// Avança TODOS os Animators da cena e aplica o TRS interpolado nos
    /// Transforms locais dos nós.
    static void update(eng::scene::Scene& scene,
                       const AnimationBank& bank, float deltaSeconds);

    /// Amostra um clip num instante (TRS out; tracks ausentes ficam com o
    /// valor default e o bit de aplicação desligado pelo chamador).
    struct Pose {
        eng::math::Vec3 position{};
        eng::math::Quat rotation{};
        eng::math::Vec3 scale{1.f, 1.f, 1.f};
    };
    [[nodiscard]] static Pose sample(const AnimationClip& clip, float time);

    /// Blend linear de poses (position/scale lerp; rotation slerp).
    [[nodiscard]] static Pose blend(const Pose& a, const Pose& b, float t);
};

}  // namespace eng::animation
