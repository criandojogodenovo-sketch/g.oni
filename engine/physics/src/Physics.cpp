#include "eng/physics/Physics.hpp"

/// Physics — implementação (FASE 10). Esfera + AABB; semi-implícito.

#include <algorithm>
#include <cmath>

namespace eng::physics {

namespace {

using eng::math::Vec3;

/// AABB do colisor no MUNDO (transform aplicado — box alinhado ao MUNDO
/// nesta fase; rotação de box é extensão documentada).
struct WorldAabb {
    Vec3 center;
    Vec3 halfExtents;
};

struct WorldShape {
    bool isSphere{false};
    Vec3 center;
    float radius{0.f};
    WorldAabb box;
};

[[nodiscard]] WorldShape worldShapeOf(const eng::scene::Scene& scene,
                                     eng::ecs::Entity entity,
                                     const Collider& collider)
{
    WorldShape shape;
    const eng::math::Mat4 world = scene.computeWorldMatrix(entity);
    shape.center = {world.at(3, 0), world.at(3, 1), world.at(3, 2)};
    shape.isSphere = collider.shape == ColliderShape::Sphere;
    if (shape.isSphere) {
        // Escala da COLUNA X aproxima o raio (correta para escala
        // uniforme; anisotrópica é limitação documentada — AABB ignora
        // rotação do nó, idem).
        const float sx = std::sqrt(world.at(0, 0) * world.at(0, 0) +
                                  world.at(0, 1) * world.at(0, 1) +
                                  world.at(0, 2) * world.at(0, 2));
        shape.radius = collider.radius * sx;
    } else {
        const float sx = std::sqrt(world.at(0, 0) * world.at(0, 0) +
                                  world.at(0, 1) * world.at(0, 1) +
                                  world.at(0, 2) * world.at(0, 2));
        const float sy = std::sqrt(world.at(1, 0) * world.at(1, 0) +
                                  world.at(1, 1) * world.at(1, 1) +
                                  world.at(1, 2) * world.at(1, 2));
        const float sz = std::sqrt(world.at(2, 0) * world.at(2, 0) +
                                  world.at(2, 1) * world.at(2, 1) +
                                  world.at(2, 2) * world.at(2, 2));
        shape.box.center = shape.center;
        shape.box.halfExtents = {collider.halfExtents.x * sx,
                                collider.halfExtents.y * sy,
                                collider.halfExtents.z * sz};
    }
    return shape;
}

/// Colisão esfera-esfera → profundidade/normal (de B para A).
[[nodiscard]] bool sphereSphere(const WorldShape& a, const WorldShape& b,
                               Vec3& normal, float& depth)
{
    const Vec3 delta = a.center - b.center;
    const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y +
                                    delta.z * delta.z);
    const float radiusSum = a.radius + b.radius;
    if (distance >= radiusSum) {
        return false;
    }
    depth = radiusSum - distance;
    normal = distance > 1e-6f
                 ? Vec3{delta.x / distance, delta.y / distance,
                        delta.z / distance}
                 : Vec3{0.f, 1.f, 0.f};
    return true;
}

/// Esfera-AABB: ponto mais próximo do centro do box.
[[nodiscard]] bool sphereAabb(const WorldShape& sphere, const WorldShape& box,
                             bool sphereIsA, Vec3& normal, float& depth)
{
    const WorldAabb& aabb = box.box;
    const Vec3 clamped = {
        std::clamp(sphere.center.x, aabb.center.x - aabb.halfExtents.x,
                   aabb.center.x + aabb.halfExtents.x),
        std::clamp(sphere.center.y, aabb.center.y - aabb.halfExtents.y,
                   aabb.center.y + aabb.halfExtents.y),
        std::clamp(sphere.center.z, aabb.center.z - aabb.halfExtents.z,
                   aabb.center.z + aabb.halfExtents.z)};
    const Vec3 delta = sphere.center - clamped;
    const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y +
                                    delta.z * delta.z);
    if (distance >= sphere.radius) {
        return false;
    }
    depth = sphere.radius - distance;
    if (distance > 1e-6f) {
        normal = {delta.x / distance, delta.y / distance,
                  delta.z / distance};
    } else {
        // Centro DENTRO do box: menor eixo para sair.
        const Vec3 local = sphere.center - aabb.center;
        const Vec3 exits = {
            aabb.halfExtents.x - std::abs(local.x),
            aabb.halfExtents.y - std::abs(local.y),
            aabb.halfExtents.z - std::abs(local.z)};
        if (exits.x <= exits.y && exits.x <= exits.z) {
            normal = {local.x >= 0.f ? 1.f : -1.f, 0.f, 0.f};
            depth = sphere.radius + exits.x;
        } else if (exits.y <= exits.z) {
            normal = {0.f, local.y >= 0.f ? 1.f : -1.f, 0.f};
            depth = sphere.radius + exits.y;
        } else {
            normal = {0.f, 0.f, local.z >= 0.f ? 1.f : -1.f};
            depth = sphere.radius + exits.z;
        }
    }
    if (!sphereIsA) {
        normal = {-normal.x, -normal.y, -normal.z}; // normal de B para A
    }
    return true;
}

[[nodiscard]] bool aabbAabb(const WorldShape& a, const WorldShape& b,
                           Vec3& normal, float& depth)
{
    const Vec3 delta = a.box.center - b.box.center;
    const Vec3 overlap = {
        a.box.halfExtents.x + b.box.halfExtents.x - std::abs(delta.x),
        a.box.halfExtents.y + b.box.halfExtents.y - std::abs(delta.y),
        a.box.halfExtents.z + b.box.halfExtents.z - std::abs(delta.z)};
    if (overlap.x <= 0.f || overlap.y <= 0.f || overlap.z <= 0.f) {
        return false;
    }
    // Menor eixo de separação.
    if (overlap.x <= overlap.y && overlap.x <= overlap.z) {
        depth = overlap.x;
        normal = {delta.x >= 0.f ? 1.f : -1.f, 0.f, 0.f};
    } else if (overlap.y <= overlap.z) {
        depth = overlap.y;
        normal = {0.f, delta.y >= 0.f ? 1.f : -1.f, 0.f};
    } else {
        depth = overlap.z;
        normal = {0.f, 0.f, delta.z >= 0.f ? 1.f : -1.f};
    }
    return true;
}

[[nodiscard]] bool shapesCollide(const WorldShape& a, const WorldShape& b,
                                Vec3& normal, float& depth)
{
    if (a.isSphere && b.isSphere) {
        return sphereSphere(a, b, normal, depth);
    }
    if (a.isSphere && !b.isSphere) {
        return sphereAabb(a, b, true, normal, depth);
    }
    if (!a.isSphere && b.isSphere) {
        // Papéis invertidos (A é box): sphereAabb já devolve a normal no
        // contrato daQUI (de B para A) via sphereIsA=false — NÃO negar de
        // novo (bug da dupla negação pego pelo teste de repouso no chão).
        return sphereAabb(b, a, false, normal, depth);
    }
    return aabbAabb(a, b, normal, depth);
}

/// Ray-sphere; t em [0, maxDistance].
[[nodiscard]] bool raySphere(const Vec3& origin, const Vec3& dir, float maxD,
                            const WorldShape& s, float& tOut)
{
    const Vec3 oc = origin - s.center;
    const float b = oc.x * dir.x + oc.y * dir.y + oc.z * dir.z;
    const float c = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z -
                    s.radius * s.radius;
    const float disc = b * b - c;
    if (disc < 0.f) {
        return false;
    }
    const float t = -b - std::sqrt(disc);
    if (t >= 0.f && t <= maxD) {
        tOut = t;
        return true;
    }
    const float t2 = -b + std::sqrt(disc);
    if (t < 0.f && t2 >= 0.f && t2 <= maxD) { // origem dentro
        tOut = t2;
        return true;
    }
    return false;
}

/// Ray-AABB (slab method).
[[nodiscard]] bool rayAabb(const Vec3& origin, const Vec3& dir, float maxD,
                          const WorldAabb& b, float& tOut)
{
    float tMin = 0.f;
    float tMax = maxD;
    for (int axis = 0; axis < 3; ++axis) {
        const float o = axis == 0 ? origin.x : axis == 1 ? origin.y : origin.z;
        const float d = axis == 0 ? dir.x : axis == 1 ? dir.y : dir.z;
        const float c = axis == 0 ? b.center.x
                       : axis == 1 ? b.center.y
                                  : b.center.z;
        const float h = axis == 0 ? b.halfExtents.x
                        : axis == 1 ? b.halfExtents.y
                                   : b.halfExtents.z;
        if (std::abs(d) < 1e-8f) {
            if (o < c - h || o > c + h) {
                return false;
            }
            continue;
        }
        float t1 = (c - h - o) / d;
        float t2 = (c + h - o) / d;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax) {
            return false;
        }
    }
    tOut = tMin;
    return true;
}

[[nodiscard]] bool masksOverlap(const Collider& a, const Collider& b)
{
    return (a.layer & b.mask) != 0u && (b.layer & a.mask) != 0u;
}

}  // namespace

// =============================================================================
// step
// =============================================================================

void PhysicsWorld::step(eng::scene::Scene& scene, float fixedDt)
{
    contacts_.clear();

    // 1) Integração semi-implícita (velocidade → posição).
    scene.world().each<RigidBody>(
        [&](eng::ecs::Entity e, RigidBody& body) {
            if (body.mass <= 0.f) {
                return; // estático
            }
            if (body.useGravity) {
                body.velocity = body.velocity + body.gravity * fixedDt;
            }
            if (body.linearDamping > 0.f) {
                const float damping =
                    1.f - std::clamp(body.linearDamping * fixedDt, 0.f, 1.f);
                body.velocity = body.velocity * damping;
            }
            auto* transform = scene.localTransform(e);
            if (transform != nullptr) {
                transform->position =
                    transform->position + body.velocity * fixedDt;
            }
        });

    // 2) Broad/narrow: TODOS os pares com colisor (n² modesto — cenas de
    //    editor/gameplay mobile; broad-phase é extensão documentada).
    std::vector<eng::ecs::Entity> collidable;
    collidable.reserve(scene.nodeCount());
    scene.world().each<Collider>([&](eng::ecs::Entity e, const Collider&) {
        if (scene.isNode(e)) {
            collidable.push_back(e);
        }
    });

    for (std::size_t i = 0; i < collidable.size(); ++i) {
        for (std::size_t j = i + 1; j < collidable.size(); ++j) {
            const eng::ecs::Entity a = collidable[i];
            const eng::ecs::Entity b = collidable[j];
            const Collider& colliderA = *scene.world().get<Collider>(a);
            const Collider& colliderB = *scene.world().get<Collider>(b);
            if (!masksOverlap(colliderA, colliderB)) {
                continue;
            }

            const WorldShape shapeA = worldShapeOf(scene, a, colliderA);
            const WorldShape shapeB = worldShapeOf(scene, b, colliderB);
            Vec3 normal;
            float depth = 0.f;
            if (!shapesCollide(shapeA, shapeB, normal, depth)) {
                continue;
            }

            const bool trigger = colliderA.isTrigger || colliderB.isTrigger;
            const Vec3 mid = shapeA.center -
                             normal * (depth * 0.5f);
            contacts_.push_back(ContactEvent{
                a, b, normal, mid, depth, trigger});
            if (trigger) {
                continue; // contato SEM resolução (§7.2)
            }

            // 3) Resolução: projeção posicional proporcional às massas
            //    inversas + impulso escalar ao longo da normal.
            RigidBody* bodyA = scene.world().get<RigidBody>(a);
            RigidBody* bodyB = scene.world().get<RigidBody>(b);
            const float invA =
                bodyA != nullptr && bodyA->mass > 0.f ? 1.f / bodyA->mass
                                                      : 0.f;
            const float invB =
                bodyB != nullptr && bodyB->mass > 0.f ? 1.f / bodyB->mass
                                                      : 0.f;
            const float invSum = invA + invB;
            if (invSum <= 0.f) {
                continue; // dois estáticos
            }
            const auto* transformA = scene.localTransform(a);
            const auto* transformB = scene.localTransform(b);
            const Vec3 push = normal * (depth / invSum);
            if (transformA != nullptr && invA > 0.f) {
                scene.localTransform(a)->position =
                    scene.localTransform(a)->position + push * invA;
            }
            if (transformB != nullptr && invB > 0.f) {
                scene.localTransform(b)->position =
                    scene.localTransform(b)->position - push * invB;
            }

            // Impulso (restituição 0 — gameplay mobile): cancela a
            // componente de aproximação ao longo da normal.
            if (bodyA != nullptr && invA > 0.f) {
                const float vn = bodyA->velocity.x * normal.x +
                                 bodyA->velocity.y * normal.y +
                                 bodyA->velocity.z * normal.z;
                if (vn < 0.f) {
                    bodyA->velocity =
                        bodyA->velocity - normal * vn;
                }
            }
            if (bodyB != nullptr && invB > 0.f) {
                const float vn = bodyB->velocity.x * normal.x +
                                 bodyB->velocity.y * normal.y +
                                 bodyB->velocity.z * normal.z;
                if (vn > 0.f) {
                    bodyB->velocity =
                        bodyB->velocity - normal * vn;
                }
            }
        }
    }
}

// =============================================================================
// raycast (§7.4)
// =============================================================================

eng::core::Result<RaycastHit> PhysicsWorld::raycast(
    const eng::scene::Scene& scene, Vec3 origin, Vec3 direction,
    float maxDistance, std::uint32_t mask)
{
    using eng::core::Error;
    using eng::core::StatusCode;
    using eng::core::makeUnexpected;

    const float len = std::sqrt(direction.x * direction.x +
                                direction.y * direction.y +
                                direction.z * direction.z);
    if (len < 1e-6f) {
        return makeUnexpected(Error{StatusCode::InvalidArgument,
                                    "raycast: direção nula"});
    }
    if (maxDistance <= 0.f) {
        return makeUnexpected(Error{StatusCode::InvalidArgument,
                                    "raycast: distância <= 0"});
    }
    const Vec3 dir = {direction.x / len, direction.y / len,
                      direction.z / len};

    RaycastHit best;
    best.hit = false;
    best.distance = maxDistance;

    scene.world().each<Collider>(
        [&](eng::ecs::Entity e, const Collider& collider) {
            if (!scene.isNode(e) || (collider.layer & mask) == 0u) {
                return;
            }
            const WorldShape shape = worldShapeOf(scene, e, collider);
            float t = 0.f;
            bool hit = shape.isSphere
                          ? raySphere(origin, dir, best.distance, shape, t)
                          : rayAabb(origin, dir, best.distance, shape.box, t);
            if (!hit || t >= best.distance) {
                return;
            }
            best.hit = true;
            best.entity = e;
            best.distance = t;
            best.point = origin + dir * t;
            // Normal: aproximação por eixo dominante da saída (documentado:
            // normal exata de superfície é refinamento futuro).
            if (shape.isSphere) {
                const Vec3 delta = best.point - shape.center;
                const float dl = std::sqrt(delta.x * delta.x +
                                           delta.y * delta.y +
                                           delta.z * delta.z);
                best.normal = dl > 1e-6f
                                  ? Vec3{delta.x / dl, delta.y / dl,
                                         delta.z / dl}
                                  : Vec3{0.f, 1.f, 0.f};
            } else {
                const Vec3 local = best.point - shape.box.center;
                const Vec3 ratio = {
                    local.x / std::max(shape.box.halfExtents.x, 1e-6f),
                    local.y / std::max(shape.box.halfExtents.y, 1e-6f),
                    local.z / std::max(shape.box.halfExtents.z, 1e-6f)};
                if (std::abs(ratio.x) >= std::abs(ratio.y) &&
                    std::abs(ratio.x) >= std::abs(ratio.z)) {
                    best.normal = {ratio.x >= 0.f ? 1.f : -1.f, 0.f, 0.f};
                } else if (std::abs(ratio.y) >= std::abs(ratio.z)) {
                    best.normal = {0.f, ratio.y >= 0.f ? 1.f : -1.f, 0.f};
                } else {
                    best.normal = {0.f, 0.f, ratio.z >= 0.f ? 1.f : -1.f};
                }
            }
        });

    if (!best.hit) {
        best.distance = 0.f;
    }
    return best;
}

// =============================================================================
// CharacterBody (§7.5)
// =============================================================================

Vec3 PhysicsWorld::moveAndSlide(const eng::scene::Scene& scene,
                                eng::ecs::Entity body, Vec3 motion)
{
    using eng::math::Vec3;
    const auto* character = scene.world().get<CharacterBody>(body);
    if (character == nullptr) {
        return motion;
    }

    // Esfera do personagem no destino proposto.
    const eng::math::Mat4 world = scene.computeWorldMatrix(body);
    Vec3 position = {world.at(3, 0), world.at(3, 1), world.at(3, 2)};
    const float radius = character->radius;

    // PASSADA ÚNICA (documentada §7.5): move ao destino; se bloqueado,
    // a projeção para fora da superfície ao longo da normal ABSORVE a
    // componente normal do movimento — o resultado é o deslize na
    // superfície. Multi-hit re-slide é extensão futura.
    const Vec3 target = position + motion;
    float deepest = 0.f;
    Vec3 pushNormal{0.f, 1.f, 0.f};
    bool collided = false;

    scene.world().each<Collider>([&](eng::ecs::Entity e,
                                     const Collider& collider) {
        if (e == body || collider.isTrigger) {
            return;
        }
        const WorldShape other = worldShapeOf(scene, e, collider);
        WorldShape self;
        self.isSphere = true;
        self.center = target;
        self.radius = radius;
        Vec3 normal;
        float depth = 0.f;
        if (!shapesCollide(self, other, normal, depth)) {
            return;
        }
        if (depth > deepest) {
            deepest = depth;
            pushNormal = normal;
            collided = true;
        }
    });

    Vec3 resolved =
        collided ? target + pushNormal * (deepest * 1.001f + 0.001f) : target;

    // Bug C-18 da auditoria final: snapToGround era serializado e nunca
    // aplicado. Semântica: com o movimento (quase) horizontal e chão a até
    // meio raio abaixo, PROJETA a esfera para pousar — personagem desce
    // rampas/degraus sem "flutuar" nos frames de queda.
    if (character->snapToGround) {
        const bool mostlyHorizontal = std::abs(motion.y) <= radius * 0.5f;
        if (mostlyHorizontal && radius > 0.f) {
            // O raio parte do CENTRO: alcance = raio (até a superfície da
            // esfera) + meio raio de folga de snap.
            const float snapDistance = radius * 1.5f;
            const auto snap = raycast(scene, resolved,
                                      Vec3{0.f, -1.f, 0.f}, snapDistance);
            if (snap.ok() && snap.value().hit) {
                const auto& hit = snap.value();
                // Só gruda em superfícies razoavelmente horizontais.
                if (hit.normal.y > 0.5f) {
                    resolved = hit.point +
                               hit.normal * (radius * 1.001f + 0.001f);
                }
            }
        }
    }
    return resolved;
}

// =============================================================================
// TimestepAccumulator (§7.6)
// =============================================================================

std::uint32_t TimestepAccumulator::advance(float frameDt) noexcept
{
    if (frameDt <= 0.f) {
        return 0;
    }
    carry_ += frameDt;
    std::uint32_t steps = 0;
    while (carry_ >= fixedDt_ && steps < 8u) { // clamp anti-espiral
        carry_ -= fixedDt_;
        ++steps;
    }
    if (carry_ >= fixedDt_) {
        carry_ = fixedDt_; // despeja o excesso (frame congelado)
    }
    return steps;
}

}  // namespace eng::physics
