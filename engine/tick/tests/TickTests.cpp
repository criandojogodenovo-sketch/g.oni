#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "eng/animation/Animation.hpp"
#include "eng/math/Vec3.hpp"
#include "eng/particles/Particles.hpp"
#include "eng/physics/Physics.hpp"
#include "eng/scene/Scene.hpp"
#include "eng/tick/Tick.hpp"

namespace {

constexpr float kEps = 1e-4f;

Catch::Approx approx(float value)
{
    return Catch::Approx(value).epsilon(kEps).margin(kEps);
}

/// Sistema de teste: registra a ordem em que rodou.
class OrderProbe final : public eng::tick::TickSystem {
public:
    OrderProbe(const char* name, eng::tick::Phase phase, int order,
               std::vector<std::string>* log)
        : name_(name), phase_(phase), order_(order), log_(log)
    {
    }

    [[nodiscard]] const char* name() const override { return name_; }
    [[nodiscard]] eng::tick::Phase phase() const override { return phase_; }
    [[nodiscard]] int order() const override { return order_; }

    void tick(eng::scene::Scene& /*scene*/, float /*dt*/) override
    {
        log_->push_back(name_);
    }

private:
    const char* name_;
    eng::tick::Phase phase_;
    int order_;
    std::vector<std::string>* log_;
};

}  // namespace

// =============================================================================
// Scheduler: ordenação determinística
// =============================================================================

TEST_CASE("tick: ordem por (fase, ordem, inserção)", "[tick]")
{
    std::vector<std::string> log;
    eng::tick::TickScheduler scheduler;

    // Inserção FORA de ordem de propósito.
    REQUIRE(scheduler
                .addSystem(std::make_unique<OrderProbe>(
                    "B-update-20", eng::tick::Phase::Update, 20, &log))
                .ok());
    REQUIRE(scheduler
                .addSystem(std::make_unique<OrderProbe>(
                    "A-preupdate", eng::tick::Phase::PreUpdate, 0, &log))
                .ok());
    REQUIRE(scheduler
                .addSystem(std::make_unique<OrderProbe>(
                    "A-update-20", eng::tick::Phase::Update, 20, &log))
                .ok());
    REQUIRE(scheduler
                .addSystem(std::make_unique<OrderProbe>(
                    "C-prerender", eng::tick::Phase::PreRender, 0, &log))
                .ok());
    REQUIRE(scheduler
                .addSystem(std::make_unique<OrderProbe>(
                    "A-update-10", eng::tick::Phase::Update, 10, &log))
                .ok());

    // systemOrder reflete a ordem de EXECUÇÃO (não de inserção).
    const auto order = scheduler.systemOrder();
    REQUIRE(order.size() == 5);
    CHECK(order[0] == "A-preupdate");    // fase 0 primeiro
    CHECK(order[1] == "A-update-10");    // ordem 10 < 20 dentro da fase
    CHECK(order[2] == "B-update-20");     // ordem igual → inserção
    CHECK(order[3] == "A-update-20");
    CHECK(order[4] == "C-prerender");

    eng::scene::Scene scene;
    scheduler.runFrame(scene, 1.f / 60.f);
    CHECK(log == order);  // execução == ordem declarada
}

TEST_CASE("tick: nome duplicado e nulo rejeitados; find/clear", "[tick]")
{
    eng::tick::TickScheduler scheduler;
    std::vector<std::string> log;

    REQUIRE(scheduler
                .addSystem(std::make_unique<OrderProbe>(
                    "dup", eng::tick::Phase::Update, 0, &log))
                .ok());
    const auto duplicate = scheduler.addSystem(std::make_unique<OrderProbe>(
        "dup", eng::tick::Phase::PreUpdate, 0, &log));
    REQUIRE(duplicate.isError());
    CHECK(duplicate.error().message.find("já existe") != std::string::npos);

    CHECK(scheduler.addSystem(nullptr).isError());

    CHECK(scheduler.find("dup") != nullptr);
    CHECK(scheduler.find("nao-existe") == nullptr);
    CHECK(scheduler.find(nullptr) == nullptr);
    CHECK(scheduler.size() == 1);

    scheduler.clear();
    CHECK(scheduler.size() == 0);
    CHECK(scheduler.find("dup") == nullptr);
}

// =============================================================================
// PhysicsTick: timestep fixo preservado (o mesmo comportamento da FASE 10)
// =============================================================================

TEST_CASE("tick: PhysicsTick avança com timestep fixo", "[tick]")
{
    eng::scene::Scene scene;
    eng::physics::PhysicsWorld world;
    eng::physics::TimestepAccumulator accumulator{1.f / 60.f};

    const auto body = scene.createNode();
    auto* rigid = scene.world().emplace<eng::physics::RigidBody>(body);
    REQUIRE(rigid != nullptr);
    rigid->mass = 1.f;
    rigid->useGravity = true;
    rigid->gravity = eng::math::Vec3{0.f, -10.f, 0.f};
    (void)scene.world().emplace<eng::physics::Collider>(body);

    eng::tick::TickScheduler scheduler;
    REQUIRE(scheduler
                .addSystem(std::make_unique<eng::tick::PhysicsTick>(
                    world, accumulator))
                .ok());

    const auto* transform = scene.localTransform(body);
    REQUIRE(transform != nullptr);
    const float y0 = transform->position.y;

    // 3 frames de 1/60 = 3 passos fixos: y = y0 - 10*(1/60)^2*... série
    // semi-implícita: v += g*dt; p += v*dt.
    for (int frame = 0; frame < 3; ++frame) {
        scheduler.runFrame(scene, 1.f / 60.f);
    }
    // v após 3 passos = -10 * 3/60 = -0.5; posição ≈ soma da série.
    const float expected = -10.f * (1.f / 60.f) * (1.f + 2.f + 3.f) / 60.f;
    CHECK(transform->position.y == approx(y0 + expected));
}

TEST_CASE("tick: PhysicsTick acumula dt menor que o passo (sem passo parcial)", "[tick]")
{
    eng::scene::Scene scene;
    eng::physics::PhysicsWorld world;
    eng::physics::TimestepAccumulator accumulator{1.f / 60.f};

    const auto body = scene.createNode();
    auto* rigid = scene.world().emplace<eng::physics::RigidBody>(body);
    REQUIRE(rigid != nullptr);
    rigid->mass = 1.f;
    rigid->useGravity = true;
    rigid->gravity = eng::math::Vec3{0.f, -10.f, 0.f};
    (void)scene.world().emplace<eng::physics::Collider>(body);

    eng::tick::TickScheduler scheduler;
    REQUIRE(scheduler
                .addSystem(std::make_unique<eng::tick::PhysicsTick>(
                    world, accumulator))
                .ok());

    const auto* transform = scene.localTransform(body);
    REQUIRE(transform != nullptr);
    const float y0 = transform->position.y;

    // 3 frames de 1/120 (< passo) = 1 passo acumulado no último frame.
    for (int frame = 0; frame < 3; ++frame) {
        scheduler.runFrame(scene, 1.f / 120.f);
    }
    CHECK(transform->position.y == approx(y0 - 10.f * (1.f / 60.f) *
                                                    (1.f / 60.f)));
}

// =============================================================================
// AnimationTick: chamada direta + camada timeScale escala por entidade
// =============================================================================

TEST_CASE("tick: AnimationTick roda e timeScale da camada escala o dt", "[tick]")
{
    eng::scene::Scene scene;
    eng::animation::AnimationBank bank;

    eng::animation::AnimationClip clip;
    clip.name = "spin";
    {
        eng::animation::PositionKey kf;
        kf.time = 0.f;
        clip.position.push_back(kf);
        eng::animation::PositionKey kf2;
        kf2.time = 2.f;
        kf2.value = eng::math::Vec3{2.f, 0.f, 0.f};
        clip.position.push_back(kf2);
    }
    bank.add(std::move(clip));

    const auto fast = scene.createNode();
    const auto slow = scene.createNode();
    // Ponteiros de pool NÃO são estáveis entre emplaces do MESMO tipo (o
    // dense array realoca — ADR-024): emplace tudo ANTES de reter.
    (void)scene.world().emplace<eng::animation::Animator>(fast);
    (void)scene.world().emplace<eng::animation::Animator>(slow);
    auto* fastAnimator = scene.world().get<eng::animation::Animator>(fast);
    auto* slowAnimator = scene.world().get<eng::animation::Animator>(slow);
    REQUIRE(fastAnimator != nullptr);
    REQUIRE(slowAnimator != nullptr);
    fastAnimator->clip = "spin";
    fastAnimator->playing = true;
    slowAnimator->clip = "spin";
    slowAnimator->playing = true;

    // slow está numa camada com timeScale 0.25.
    REQUIRE(scene.layers().setTimeScale("SUBGAME", 0.25f).ok());
    (void)scene.world().emplace<eng::scene::LayerMember>(
        slow, eng::scene::LayerMember{"SUBGAME"});

    eng::tick::TickScheduler scheduler;
    REQUIRE(scheduler
                .addSystem(std::make_unique<eng::tick::AnimationTick>(bank))
                .ok());

    scheduler.runFrame(scene, 1.f);  // 1 segundo de frame
    CHECK(fastAnimator->time == approx(1.f));
    CHECK(slowAnimator->time == approx(0.25f));
}

// =============================================================================
// ParticleTick: camada sem update congela o emissor
// =============================================================================

TEST_CASE("tick: ParticleTick respeita camada sem update", "[tick]")
{
    eng::scene::Scene scene;

    eng::particles::ParticleEmitter emitter;
    emitter.rate = 60.f;
    emitter.playing = true;
    emitter.lifetime = 1.f;
    emitter.speed = 1.f;

    const auto live = scene.createNode();
    const auto frozen = scene.createNode();
    (void)scene.world().emplace<eng::particles::ParticleEmitter>(live,
                                                                emitter);
    (void)scene.world().emplace<eng::particles::ParticleEmitter>(frozen,
                                                                emitter);

    eng::scene::LayerParticipation off;
    off.update = false;
    REQUIRE(scene.layers().setParticipation("SUBGAME", off).ok());
    (void)scene.world().emplace<eng::scene::LayerMember>(
        frozen, eng::scene::LayerMember{"SUBGAME"});

    eng::tick::TickScheduler scheduler;
    REQUIRE(scheduler
                .addSystem(std::make_unique<eng::tick::ParticleTick>())
                .ok());

    scheduler.runFrame(scene, 0.5f);
    CHECK(eng::particles::ParticleSystem::aliveCount(scene) > 0);  // live
    // frozen: 0 partículas vivas (pool nem nasce)
    const auto* frozenPool =
        scene.world().get<eng::particles::ParticlePool>(frozen);
    CHECK((frozenPool == nullptr || frozenPool->particles.empty()));
}
