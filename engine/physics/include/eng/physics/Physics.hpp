#pragma once

/// eng::physics — física mínima correta sobre o ECS (FASE 10, missão §7).
///
/// Primitivas: esfera + AABB (§D1 — rotação de box é extensão futura).
/// Integração semi-implícita de Euler; mass == 0 → corpo ESTÁTICO;
/// resolução por projeção posicional + impulso escalar (sem rotação de
/// corpo — sem inércia angular nesta fase, documentado).
///
/// TIMESTEP (§7.6): `step(scene, fixedDt)` com dt FIXO — o chamador
/// acumula o dt do frame e dá N passos (ver TimestepAccumulator).
///
/// Nada aqui conhece RHI/Android (§7 intro).

#include <cstdint>
#include <span>
#include <vector>

#include "eng/core/Result.hpp"
#include "eng/ecs/Ecs.hpp"
#include "eng/math/Mat4.hpp"
#include "eng/math/Vec3.hpp"
#include "eng/reflect/Reflect.hpp"
#include "eng/scene/Scene.hpp"

namespace eng::physics {

// =============================================================================
// Componentes (§7.1–§7.3/§7.5) — refletidos p/ inspector/serialização
// =============================================================================

struct RigidBody {
    float mass{1.f};              ///< 0 = estático (colisor fixo)
    eng::math::Vec3 velocity{0.f, 0.f, 0.f};
    eng::math::Vec3 gravity{0.f, -9.81f, 0.f};
    bool useGravity{true};
    float linearDamping{0.f};     ///< 0..1 por segundo
};

/// Forma do colisor (enum de namespace — o reflect cobre enums de
/// namespace; nested quebra o traço de nome canônico, ADR-021).
enum class ColliderShape : std::uint8_t { Sphere = 0, Box = 1 };

struct Collider {
    ColliderShape shape{ColliderShape::Sphere};
    float radius{0.5f};                 ///< Sphere
    eng::math::Vec3 halfExtents{0.5f, 0.5f, 0.5f}; ///< Box (AABB local)
    std::uint32_t layer{1};             ///< bit(s) próprio(s)
    std::uint32_t mask{0xFFFFFFFFu};    ///< com quem colide
    bool isTrigger{false};              ///< contato SEM resolução (§7.2)
};

/// Controle de personagem mobile (§7.5): esfera que MOVE E DESLIZA
/// contra estáticos — física de gameplay, não simulação completa.
struct CharacterBody {
    eng::math::Vec3 velocity{0.f, 0.f, 0.f};
    float radius{0.5f};
    bool snapToGround{false}; ///< projeta para o chão (remediação C-18:
                              ///< movimento horizontal + chão a meio raio)
};

// Registro reflect (nomes estáveis — ADR-021/033).
ENG_REFLECT_BEGIN(eng::physics::RigidBody)
    ENG_REFLECT_FIELD(mass)
    ENG_REFLECT_FIELD_AS(velocity, "eng::math::Vec3")
    ENG_REFLECT_FIELD_AS(gravity, "eng::math::Vec3")
    ENG_REFLECT_FIELD(useGravity)
    ENG_REFLECT_FIELD(linearDamping)
ENG_REFLECT_END()

// BUG FIX (evolução P0-6): era ENG_REFLECT_BEGIN (STRUCT) — o enum era
// registrado como struct sem propriedades: o campo `shape` do Collider
// NUNCA apareceu no Inspector nem foi serializado (recursão em struct
// vazia = silêncio). O macro correto registra kind=Enum + subjacente.
ENG_REFLECT_ENUM_BEGIN(eng::physics::ColliderShape)
    ENG_REFLECT_ENUM_VALUE(Sphere)
    ENG_REFLECT_ENUM_VALUE(Box)
ENG_REFLECT_ENUM_END()

ENG_REFLECT_BEGIN(eng::physics::Collider)
    ENG_REFLECT_FIELD_AS(shape, "eng::physics::ColliderShape")
    ENG_REFLECT_FIELD(radius)
    ENG_REFLECT_FIELD_AS(halfExtents, "eng::math::Vec3")
    ENG_REFLECT_FIELD(layer)
    ENG_REFLECT_FIELD(mask)
    ENG_REFLECT_FIELD(isTrigger)
ENG_REFLECT_END()

ENG_REFLECT_BEGIN(eng::physics::CharacterBody)
    ENG_REFLECT_FIELD_AS(velocity, "eng::math::Vec3")
    ENG_REFLECT_FIELD(radius)
    ENG_REFLECT_FIELD(snapToGround)
ENG_REFLECT_END()

// =============================================================================
// Contatos (§7.2)
// =============================================================================

struct ContactEvent {
    eng::ecs::Entity entityA{};
    eng::ecs::Entity entityB{};
    eng::math::Vec3 normal{0.f, 1.f, 0.f}; ///< de B para A
    eng::math::Vec3 point{0.f, 0.f, 0.f};
    float depth{0.f};
    bool isTrigger{false};
};

// =============================================================================
// Raycast (§7.4)
// =============================================================================

struct RaycastHit {
    bool hit{false};
    eng::ecs::Entity entity{};
    eng::math::Vec3 point{0.f, 0.f, 0.f};
    eng::math::Vec3 normal{0.f, 0.f, 0.f};
    float distance{0.f};
};

// =============================================================================
// PhysicsWorld — sistema sobre a cena
// =============================================================================

class PhysicsWorld final {
public:
    /// Um passo FIXO (§7.6): integra → detecta → resolve (não-re triggers).
    /// `fixedDt` deve ser constante entre chamadas (ex.: 1/60).
    void step(eng::scene::Scene& scene, float fixedDt);

    /// Raio contra TODOS os colisores (o MAIS PRÓXIMO vence). Direção
    /// normalizada exigida (erro preciso caso contrário).
    [[nodiscard]] static eng::core::Result<RaycastHit> raycast(
        const eng::scene::Scene& scene, eng::math::Vec3 origin,
        eng::math::Vec3 direction, float maxDistance,
        std::uint32_t mask = 0xFFFFFFFFu);

    /// Movimento + deslize do CharacterBody contra estáticos/dinâmicos
    /// (retorna a posição FINAL em mundo — o chamador aplica ao
    /// transform; `motion` é o deslocamento proposto, NÃO consumido de
    /// CharacterBody::velocity; com snapToGround, projeta ao chão).
    [[nodiscard]] static eng::math::Vec3 moveAndSlide(
        const eng::scene::Scene& scene, eng::ecs::Entity body,
        eng::math::Vec3 motion);

    [[nodiscard]] std::span<const ContactEvent> contacts() const noexcept
    {
        return contacts_;
    }
    [[nodiscard]] std::size_t contactCount() const noexcept
    {
        return contacts_.size();
    }

    void clearContacts() { contacts_.clear(); }

private:
    std::vector<ContactEvent> contacts_;
};

/// Acumulador de timestep fixo (§7.6 — frame dt variável → passos fixos).
class TimestepAccumulator final {
public:
    explicit TimestepAccumulator(float fixedDt = 1.f / 60.f) noexcept
        : fixedDt_(fixedDt)
    {
    }

    /// Alimenta o dt do frame; devolve QUANTOS passos fixos executar.
    std::uint32_t advance(float frameDt) noexcept;

    [[nodiscard]] float fixedDt() const noexcept { return fixedDt_; }
    void setFixedDt(float fixedDt) noexcept
    {
        fixedDt_ = fixedDt > 0.f ? fixedDt : 1.f / 60.f;
    }
    [[nodiscard]] float carry() const noexcept { return carry_; }
    void reset() noexcept { carry_ = 0.f; }

private:
    float fixedDt_;
    float carry_{0.f};
};

}  // namespace eng::physics
