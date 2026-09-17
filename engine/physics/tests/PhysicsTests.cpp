/// Testes de eng::physics (FASE 10, missão §7.14).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "eng/physics/Physics.hpp"

namespace {

using namespace eng::physics;
using eng::math::Vec3;

struct WorldFixture {
    eng::scene::Scene scene;
    PhysicsWorld physics;

    eng::ecs::Entity addBody(Vec3 position, float mass = 1.f,
                             float radius = 0.5f, Vec3 gravity = {0.f, -9.81f, 0.f},
                             bool useGravity = true) {
        const auto e = scene.createNode();
        (void)scene.world().emplace<RigidBody>(
            e, RigidBody{mass, {}, gravity, useGravity});
        Collider collider;
        collider.shape = ColliderShape::Sphere;
        collider.radius = radius;
        (void)scene.world().emplace<Collider>(e, collider);
        scene.localTransform(e)->position = position;
        return e;
    }

    eng::ecs::Entity addStaticBox(Vec3 position, Vec3 halfExtents) {
        const auto e = scene.createNode();
        RigidBody body; // sem RigidBody: só colisor estático
        (void)body;
        Collider collider;
        collider.shape = ColliderShape::Box;
        collider.halfExtents = halfExtents;
        (void)scene.world().emplace<Collider>(e, collider);
        scene.localTransform(e)->position = position;
        return e;
    }
};

}  // namespace

// =============================================================================
// Corpos (§7.3)
// =============================================================================

TEST_CASE("physics: massa zero é estático; gravidade integra", "[physics]")
{
    WorldFixture f;
    auto body = f.addBody({0.f, 10.f, 0.f});
    f.physics.step(f.scene, 1.f / 60.f);
    const RigidBody* rb = f.scene.world().get<RigidBody>(body);
    REQUIRE(rb != nullptr);
    CHECK(rb->velocity.y == Catch::Approx(-9.81f / 60.f).margin(1e-5f));
    const float y = f.scene.localTransform(body)->position.y;
    CHECK(y < 10.f); // caiu
    CHECK(y > 10.f - 0.005f); // mas nem um passo inteiro (dt²)

    // Sem gravidade: parado (afastado do primeiro corpo).
    auto floating = f.addBody({5.f, 10.f, 0.f}, 1.f, 0.5f, {0.f, 0.f, 0.f});
    f.physics.step(f.scene, 1.f / 60.f);
    CHECK(f.scene.localTransform(floating)->position.y == Catch::Approx(10.f));
}

TEST_CASE("physics: velocity/impulso/força por passo fixo", "[physics]")
{
    WorldFixture f;
    auto body = f.addBody({0.f, 0.f, 0.f}, 1.f, 0.5f, {0.f, 0.f, 0.f});
    auto* rb = f.scene.world().get<RigidBody>(body);

    // "setVelocity" direto no componente (API de gameplay = componente).
    rb->velocity = {2.f, 0.f, 0.f};
    f.physics.step(f.scene, 0.5f);
    CHECK(f.scene.localTransform(body)->position.x == Catch::Approx(1.f));

    // Força equivalente: a = F/m aplicada por dt (via gravidade do corpo —
    // mesma matemática do integrador).
    rb->gravity = {0.f, -2.f, 0.f}; // a = -2 m/s²
    rb->velocity = {0.f, 0.f, 0.f};
    f.physics.step(f.scene, 1.f);
    CHECK(rb->velocity.y == Catch::Approx(-2.f).margin(1e-5f));

    // Impulso = Δv instantâneo (massa 1): v += J.
    rb->gravity = {0.f, 0.f, 0.f};
    rb->velocity = rb->velocity + Vec3{0.f, 5.f, 0.f}; // J = (0,5,0)
    CHECK(rb->velocity.y == Catch::Approx(3.f)); // -2 + 5
}

// =============================================================================
// Colisões (§7.2)
// =============================================================================

TEST_CASE("physics: esfera-esfera resolve e reporta contato", "[physics]")
{
    WorldFixture f;
    auto a = f.addBody({-0.35f, 0.f, 0.f});
    auto b = f.addBody({0.35f, 0.f, 0.f});
    f.physics.step(f.scene, 1.f / 60.f);
    // r=0.5+0.5, distância 0.7 → overlap 0.3; separação total 1.0.
    const float ax = f.scene.localTransform(a)->position.x;
    const float bx = f.scene.localTransform(b)->position.x;
    CHECK(bx - ax >= 0.99f);
    REQUIRE(f.physics.contactCount() == 1);
    const auto& contact = f.physics.contacts()[0];
    CHECK(contact.isTrigger == false);
    // normal "de B para A": a está à esquerda → aponta -X.
    CHECK(contact.normal.x == Catch::Approx(-1.f).margin(1e-3f));
    CHECK(contact.depth == Catch::Approx(0.3f).margin(1e-3f));
}

TEST_CASE("physics: trigger gera contato SEM resolução", "[physics]")
{
    WorldFixture f;
    auto player = f.addBody({0.f, 0.f, 0.f});
    auto zone = f.addBody({0.1f, 0.f, 0.f});
    f.scene.world().get<Collider>(zone)->isTrigger = true;

    f.physics.step(f.scene, 1.f / 60.f);
    REQUIRE(f.physics.contactCount() == 1);
    CHECK(f.physics.contacts()[0].isTrigger);
    // Posições INALTERADAS (sem resolução).
    CHECK(f.scene.localTransform(player)->position.x ==
          Catch::Approx(0.f));
    CHECK(f.scene.localTransform(zone)->position.x ==
          Catch::Approx(0.1f));
}

TEST_CASE("physics: layers/masks filtram pares", "[physics]")
{
    WorldFixture f;
    (void)f.addBody({0.f, 0.f, 0.f}); // layer 1 (default)
    auto ghost = f.addBody({0.1f, 0.f, 0.f});
    // Ghost na layer 2 que NÃO colide com a layer 1 (mask só bit 2).
    auto* ghostCollider = f.scene.world().get<Collider>(ghost);
    ghostCollider->layer = 2u;
    ghostCollider->mask = 2u;

    f.physics.step(f.scene, 1.f / 60.f);
    CHECK(f.physics.contactCount() == 0); // filtrados
    CHECK(f.scene.localTransform(ghost)->position.x ==
          Catch::Approx(0.1f));
}

TEST_CASE("physics: dinâmico repousa sobre estático (AABB)", "[physics]")
{
    WorldFixture f;
    auto ground = f.addStaticBox({0.f, -1.f, 0.f}, {5.f, 1.f, 5.f});
    auto body = f.addBody({0.f, 0.1f, 0.f}); // esfera r=.5 sobre o chão
    f.physics.step(f.scene, 1.f / 60.f);
    (void)ground;

    // Não atravessa o chão (top do box em y=0; centro >= 0.5-epsilon).
    CHECK(f.scene.localTransform(body)->position.y >= 0.49f);
    // Velocidade de queda cancelada pelo impulso.
    CHECK(f.scene.world().get<RigidBody>(body)->velocity.y >= 0.f);
    REQUIRE(f.physics.contactCount() == 1);
    // normal "de B para A": chão criado primeiro (A) → aponta para o chão.
    CHECK(f.physics.contacts()[0].normal.y == Catch::Approx(-1.f).margin(1e-3f));
}

TEST_CASE("physics: esfera-AABB e AABB-AABB", "[physics]")
{
    WorldFixture f;
    (void)f.addStaticBox({0.f, 0.f, 0.f}, {1.f, 1.f, 1.f});
    (void)f.addStaticBox({1.5f, 0.f, 0.f}, {1.f, 1.f, 1.f});
    f.physics.step(f.scene, 1.f / 60.f);
    // Dois estáticos: contato reportado, sem resolução de movimento.
    REQUIRE(f.physics.contactCount() >= 1);

    // Esfera dinâmica caindo no box1.
    auto ball = f.addBody({0.f, 1.6f, 0.f}, 1.f, 0.25f);
    f.physics.step(f.scene, 1.f / 60.f);
    f.physics.step(f.scene, 1.f / 60.f);
    CHECK(f.scene.localTransform(ball)->position.y > 0.7f); // não afundou
}

// =============================================================================
// Raycast (§7.4)
// =============================================================================

TEST_CASE("physics: raycast acerta o mais próximo com ponto/normal", "[physics]")
{
    WorldFixture f;
    auto near = f.addStaticBox({0.f, 0.f, -5.f}, {1.f, 1.f, 1.f});
    auto far = f.addStaticBox({0.f, 0.f, -15.f}, {1.f, 1.f, 1.f});
    auto hitResult = PhysicsWorld::raycast(f.scene, {0.f, 0.f, 0.f},
                                           {0.f, 0.f, -1.f}, 100.f);
    REQUIRE(hitResult.ok());
    REQUIRE(hitResult.value().hit);
    CHECK(hitResult.value().entity == near);
    CHECK(hitResult.value().distance == Catch::Approx(4.f).margin(1e-3f));
    CHECK(hitResult.value().point.z == Catch::Approx(-4.f).margin(1e-3f));
    CHECK(hitResult.value().normal.z == Catch::Approx(1.f).margin(1e-3f));
    (void)far;
}

TEST_CASE("physics: raycast miss/mask/erros", "[physics]")
{
    WorldFixture f;
    auto target = f.addStaticBox({10.f, 0.f, 0.f}, {1.f, 1.f, 1.f});

    auto miss = PhysicsWorld::raycast(f.scene, {0.f, 0.f, 0.f},
                                      {0.f, 0.f, -1.f}, 100.f);
    REQUIRE(miss.ok());
    CHECK_FALSE(miss.value().hit);

    // Fora do alcance.
    auto shortRay = PhysicsWorld::raycast(f.scene, {0.f, 0.f, 0.f},
                                         {1.f, 0.f, 0.f}, 5.f);
    REQUIRE(shortRay.ok());
    CHECK_FALSE(shortRay.value().hit);

    // Mask exclui a layer do alvo.
    auto masked = PhysicsWorld::raycast(f.scene, {0.f, 0.f, 0.f},
                                        {1.f, 0.f, 0.f}, 100.f, 0x2u);
    REQUIRE(masked.ok());
    CHECK_FALSE(masked.value().hit);
    (void)target;

    // Erros precisos.
    CHECK(PhysicsWorld::raycast(f.scene, {0, 0, 0}, {0, 0, 0}, 10.f).isError());
    CHECK(PhysicsWorld::raycast(f.scene, {0, 0, 0}, {1, 0, 0}, -1.f).isError());
}

TEST_CASE("physics: raycast em esfera (dentro/fora)", "[physics]")
{
    WorldFixture f;
    auto ball = f.addBody({0.f, 0.f, -5.f}, 0.f, 1.f, {0, 0, 0}, false);
    auto hit = PhysicsWorld::raycast(f.scene, {0.f, 0.f, 0.f},
                                     {0.f, 0.f, -1.f}, 100.f);
    REQUIRE(hit.ok());
    REQUIRE(hit.value().hit);
    CHECK(hit.value().entity == ball);
    CHECK(hit.value().distance == Catch::Approx(4.f).margin(1e-3f));
    CHECK(hit.value().normal.z == Catch::Approx(1.f).margin(1e-3f));
}

// =============================================================================
// Timestep fixo (§7.6/§7.14)
// =============================================================================

TEST_CASE("physics: timestep fixo é determinístico entre fatiamentos",
          "[physics]")
{
    // Mesmo tempo total, fatiamentos diferentes de frame → MESMA altura.
    auto run = [](std::vector<float> frameDts) {
        WorldFixture f;
        auto body = f.addBody({0.f, 100.f, 0.f});
        TimestepAccumulator acc(1.f / 60.f);
        for (float dt : frameDts) {
            const auto steps = acc.advance(dt);
            for (std::uint32_t s = 0; s < steps; ++s) {
                f.physics.step(f.scene, acc.fixedDt());
            }
        }
        return f.scene.localTransform(body)->position.y;
    };

    const float a = run({1.f / 60.f, 1.f / 60.f, 1.f / 60.f});
    const float b = run({1.f / 30.f, 1.f / 60.f}); // frame maior = 2 passos
    const float c = run({0.05f}); // carrega 3 passos no próximo frame
    CHECK(a == Catch::Approx(b).margin(1e-4f));
    CHECK(a != Catch::Approx(c).margin(1e-4f)); // 3 passos ≠ 2 passos
}

TEST_CASE("physics: acumulador não explode com frame congelado", "[physics]")
{
    TimestepAccumulator acc(1.f / 60.f);
    CHECK(acc.advance(0.f) == 0u);
    CHECK(acc.advance(-1.f) == 0u);
    // Um frame de 10s: 8 passos máximos + carry drenado.
    CHECK(acc.advance(10.f) == 8u);
    CHECK(acc.carry() <= acc.fixedDt());
}

// =============================================================================
// CharacterBody (§7.5)
// =============================================================================

TEST_CASE("physics: character body desliza contra parede", "[physics]")
{
    WorldFixture f;
    auto wall = f.addStaticBox({3.f, 0.f, 0.f}, {1.f, 5.f, 1.f});
    auto character = f.addBody({0.f, 0.f, 0.f}, 0.f, 0.5f, {0, 0, 0}, false);
    (void)f.scene.world().emplace<CharacterBody>(
        character, CharacterBody{{0.f, 0.f, 0.f}, 0.5f});
    // Sem RigidBody (kinematic): remove para o teste do slide puro.
    f.scene.world().remove<RigidBody>(character);

    const Vec3 finalPos = PhysicsWorld::moveAndSlide(
        f.scene, character, {2.0f, 0.f, 1.f});
    // Face da parede em x=2; esfera r=0.5 → centro para em ~1.5.
    CHECK(finalPos.x == Catch::Approx(1.5f).margin(5e-3f));
    // O componente Z (deslize) foi preservado INTEGRO.
    CHECK(finalPos.z == Catch::Approx(1.f).margin(1e-3f));
    (void)wall;
}

TEST_CASE("physics: character body move livre sem obstáculo", "[physics]")
{
    WorldFixture f;
    auto character = f.addBody({0.f, 0.f, 0.f}, 0.f, 0.5f, {0, 0, 0}, false);
    f.scene.world().remove<RigidBody>(character);
    const Vec3 finalPos = PhysicsWorld::moveAndSlide(
        f.scene, character, {1.f, 2.f, 3.f});
    CHECK(finalPos.x == Catch::Approx(1.f).margin(1e-5f));
    CHECK(finalPos.y == Catch::Approx(2.f).margin(1e-5f));
    CHECK(finalPos.z == Catch::Approx(3.f).margin(1e-5f));
}
