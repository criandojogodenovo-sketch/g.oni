#include "eng/animation/Animation.hpp"

/// Animation — implementação (FASE 10).

#include <algorithm>
#include <cmath>
#include <utility>

namespace eng::animation {

using eng::math::Quat;
using eng::math::Vec3;

/// Interpolação por tipo (antes de TODO uso — position/scale lerp,
/// rotation SLERP — §7.8).
[[nodiscard]] Vec3 blendValue(const Vec3& a, const Vec3& b, float t) noexcept
{
    return a + (b - a) * t;
}

[[nodiscard]] Quat blendValue(const Quat& a, const Quat& b, float t) noexcept
{
    return a.slerp(b, t);
}

namespace {

/// Amostra uma track ordenada por tempo (busca linear — clips curtos).
template <typename Key>
[[nodiscard]] typename Key::value_type sampleTrack(
    const std::vector<Key>& keys, float time)
{
    if (keys.empty()) {
        return {};
    }
    if (time <= keys.front().time || keys.size() == 1u) {
        return keys.front().value;
    }
    if (time >= keys.back().time) {
        return keys.back().value;
    }
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const Key& a = keys[i];
        const Key& b = keys[i + 1];
        if (time >= a.time && time <= b.time) {
            const float span = b.time - a.time;
            const float t =
                span > 1e-6f ? (time - a.time) / span : 0.f;
            return blendValue(a.value, b.value, t);
        }
    }
    return keys.back().value;
}

}  // namespace

// =============================================================================
// AnimationClip / Bank
// =============================================================================

float AnimationClip::duration() const noexcept
{
    float end = 0.f;
    if (!position.empty()) {
        end = std::max(end, position.back().time);
    }
    if (!rotation.empty()) {
        end = std::max(end, rotation.back().time);
    }
    if (!scale.empty()) {
        end = std::max(end, scale.back().time);
    }
    return end;
}

void AnimationBank::add(AnimationClip clip)
{
    const std::string key = clip.name;
    clips_.insert_or_assign(std::move(key), std::move(clip));
}

const AnimationClip* AnimationBank::find(std::string_view name) const noexcept
{
    const auto it = clips_.find(std::string(name));
    return it == clips_.end() ? nullptr : &it->second;
}

// =============================================================================
// AnimatorStateMachine (§7.10)
// =============================================================================

void AnimatorStateMachine::transition(const AnimationBank& bank,
                                     std::string_view next,
                                     float blendDuration)
{
    if (state_.current.empty()) {
        state_.current = animator_.clip; // sincroniza com o componente
    }
    if (state_.current == next) {
        return; // já está nele
    }
    if (bank.find(next) == nullptr) {
        return; // clip desconhecido: no-op seguro (documentado)
    }
    if (blendDuration <= 0.f || state_.current.empty()) {
        // Troca seca (sem estado anterior válido).
        animator_.clip = std::string(next);
        animator_.time = 0.f;
        animator_.playing = true;
        state_.current = std::string(next);
        state_.previous.clear();
        state_.blendRemaining = 0.f;
        return;
    }
    state_.previous = state_.current;
    state_.previousTime = animator_.time;
    state_.blendDuration = blendDuration;
    state_.blendRemaining = blendDuration;
    animator_.clip = std::string(next);
    animator_.time = 0.f;
    animator_.playing = true;
    state_.current = std::string(next);
}

void AnimatorStateMachine::update(const AnimationBank& bank,
                                 float deltaSeconds)
{
    (void)bank; // (validação de clip acontece na transition)
    if (state_.blendRemaining > 0.f) {
        state_.blendRemaining =
            std::max(0.f, state_.blendRemaining - deltaSeconds);
        if (state_.blendRemaining == 0.f) {
            state_.previous.clear();
        }
    }
}

// =============================================================================
// AnimationSystem (§7.8/§7.9)
// =============================================================================

AnimationSystem::Pose AnimationSystem::sample(const AnimationClip& clip,
                                               float time)
{
    Pose pose;
    pose.position = sampleTrack(clip.position, time);
    pose.rotation = sampleTrack(clip.rotation, time);
    pose.scale = sampleTrack(clip.scale, time);
    if (clip.scale.empty()) {
        pose.scale = {1.f, 1.f, 1.f};
    }
    return pose;
}

AnimationSystem::Pose AnimationSystem::blend(const Pose& a, const Pose& b,
                                             float t)
{
    const float clamped = std::clamp(t, 0.f, 1.f);
    Pose out;
    out.position = blendValue(a.position, b.position, clamped);
    out.rotation = a.rotation.slerp(b.rotation, clamped);
    out.scale = blendValue(a.scale, b.scale, clamped);
    return out;
}

void AnimationSystem::update(eng::scene::Scene& scene,
                             const AnimationBank& bank, float deltaSeconds)
{
    scene.world().each<Animator>([&](eng::ecs::Entity e, Animator& animator) {
        const AnimationClip* clip = bank.find(animator.clip);
        if (clip == nullptr || clip->duration() <= 0.f) {
            return; // sem clip válido: congela (sem crash)
        }

        // Avanço com velocidade (§7.9 speed) — APENAS tocando; pausado
        // mantém o cursor (seek continua aplicando a pose do instante).
        if (animator.playing) {
            animator.time += deltaSeconds * animator.speed;
            if (animator.loop) {
                animator.time = std::fmod(animator.time, clip->duration());
            } else if (animator.time >= clip->duration()) {
                animator.time = clip->duration();
                animator.playing = false; // fim (§7.9 stop natural)
            }
        }

        const Pose pose = sample(*clip, animator.time);
        auto* transform = scene.localTransform(e);
        if (transform == nullptr) {
            return;
        }
        if (animator.applyPosition) {
            transform->position = pose.position;
        }
        if (animator.applyRotation) {
            transform->rotation = pose.rotation;
        }
        if (animator.applyScale) {
            transform->scale = pose.scale;
        }
    });
}

}  // namespace eng::animation
