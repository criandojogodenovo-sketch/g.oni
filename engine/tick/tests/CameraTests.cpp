#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "eng/scene/Scene.hpp"
#include "eng/tick/Camera.hpp"

namespace {

constexpr float kEps = 1e-4f;

Catch::Approx approx(float value)
{
    return Catch::Approx(value).epsilon(kEps).margin(kEps);
}

}  // namespace

// =============================================================================
// resolveActiveCamera
// =============================================================================

TEST_CASE("camera: cena sem câmera resolve vazio", "[tick][camera]")
{
    eng::scene::Scene scene;
    (void)scene.createNode();
    (void)scene.createNode();

    const auto active = eng::tick::resolveActiveCamera(scene);
    CHECK_FALSE(active.found());
    CHECK(eng::tick::activeCameraCount(scene) == 0);
}

TEST_CASE("camera: primeira câmera ativa em ordem de criação vence", "[tick][camera]")
{
    eng::scene::Scene scene;
    const auto first = scene.createNode();
    const auto second = scene.createNode();

    auto* firstCamera = scene.world().emplace<eng::tick::CameraData>(first);
    REQUIRE(firstCamera != nullptr);
    firstCamera->posX = 5.f;
    firstCamera->posY = -3.f;
    firstCamera->zoom = 96.f;

    auto* secondCamera =
        scene.world().emplace<eng::tick::CameraData>(second);
    REQUIRE(secondCamera != nullptr);
    secondCamera->posX = 100.f;

    const auto active = eng::tick::resolveActiveCamera(scene);
    REQUIRE(active.found());
    CHECK(active.entity == first);            // ordem de criação
    CHECK(active.data.posX == approx(5.f));  // a PRIMEIRA vence
    CHECK(active.data.posY == approx(-3.f));
    CHECK(active.data.zoom == approx(96.f));
    CHECK(eng::tick::activeCameraCount(scene) == 2);
}

TEST_CASE("camera: inativas são puladas; active=false desliga sem remover", "[tick][camera]")
{
    eng::scene::Scene scene;
    const auto first = scene.createNode();
    const auto second = scene.createNode();

    // Pool NÃO é estável entre emplaces (dense array realoca — ADR-024):
    // emplace tudo, depois reter ponteiros.
    (void)scene.world().emplace<eng::tick::CameraData>(first);
    (void)scene.world().emplace<eng::tick::CameraData>(second);
    auto* firstCamera = scene.world().get<eng::tick::CameraData>(first);
    auto* secondCamera = scene.world().get<eng::tick::CameraData>(second);
    REQUIRE(firstCamera != nullptr);
    REQUIRE(secondCamera != nullptr);
    firstCamera->posX = 1.f;
    firstCamera->active = false;  // desligada
    secondCamera->posX = 2.f;

    const auto active = eng::tick::resolveActiveCamera(scene);
    REQUIRE(active.found());
    CHECK(active.entity == second);
    CHECK(active.data.posX == approx(2.f));
    CHECK(eng::tick::activeCameraCount(scene) == 1);

    // Religa a primeira → volta a vencer (ordem de criação).
    firstCamera->active = true;
    const auto reactivated = eng::tick::resolveActiveCamera(scene);
    REQUIRE(reactivated.found());
    CHECK(reactivated.entity == first);
}

// =============================================================================
// CameraTickSystem
// =============================================================================

TEST_CASE("camera: CameraTickSystem cacheia a câmera ativa do frame", "[tick][camera]")
{
    eng::scene::Scene scene;
    eng::tick::TickScheduler scheduler;
    REQUIRE(scheduler
                .addSystem(std::make_unique<eng::tick::CameraTickSystem>())
                .ok());
    CHECK(scheduler.systemOrder() ==
          std::vector<std::string>{"CameraTick"});

    // Antes de qualquer frame: vazia.
    const auto* system = scheduler.find("CameraTick");
    REQUIRE(system != nullptr);
    const auto* cameraTick =
        static_cast<const eng::tick::CameraTickSystem*>(system);
    CHECK_FALSE(cameraTick->activeCamera().found());

    // Sem câmera na cena → continua vazia após o frame.
    scheduler.runFrame(scene, 1.f / 60.f);
    CHECK_FALSE(cameraTick->activeCamera().found());

    // Câmera entra na cena → próximo frame cacheia.
    const auto cam = scene.createNode();
    auto* data = scene.world().emplace<eng::tick::CameraData>(cam);
    REQUIRE(data != nullptr);
    data->posX = 7.f;
    data->zoom = 64.f;

    scheduler.runFrame(scene, 1.f / 60.f);
    const auto& active = cameraTick->activeCamera();
    REQUIRE(active.found());
    CHECK(active.entity == cam);
    CHECK(active.data.posX == approx(7.f));
    CHECK(active.data.zoom == approx(64.f));

    // Câmera é desativada → o frame seguinte reflete.
    data->active = false;
    scheduler.runFrame(scene, 1.f / 60.f);
    CHECK_FALSE(cameraTick->activeCamera().found());
}

// =============================================================================
// CameraData: defaults (contrato com o viewport: zoom = pixels/unidade)
// =============================================================================

TEST_CASE("camera: CameraData defaults — origem, zoom 48, ativa", "[tick][camera]")
{
    const eng::tick::CameraData data;
    CHECK(data.posX == approx(0.f));
    CHECK(data.posY == approx(0.f));
    CHECK(data.zoom == approx(48.f));  // idem Viewport::Camera2D default
    CHECK(data.active);
}
