#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "eng/scene/Layers.hpp"
#include "eng/scene/Scene.hpp"
#include "eng/scene/SceneSerializer.hpp"

namespace {

constexpr float kEps = 1e-4f;

Catch::Approx approx(float value)
{
    return Catch::Approx(value).epsilon(kEps).margin(kEps);
}

} // namespace

// =============================================================================
// Registry: built-ins, nomeadas, flags
// =============================================================================

TEST_CASE("layers: built-ins GAME/SUBGAME existem com defaults", "[scene][layers]")
{
    eng::scene::LayerRegistry registry;

    CHECK(registry.has("GAME"));
    CHECK(registry.has("SUBGAME"));
    REQUIRE(registry.find("GAME") != nullptr);
    CHECK(registry.find("GAME")->timeScale == 1.f);
    CHECK(registry.find("GAME")->participation.update);
    CHECK(registry.find("GAME")->participation.physics);
    CHECK(registry.find("GAME")->participation.render);
    CHECK(registry.definitions().size() == 2);

    // built-ins são permanentes
    CHECK(registry.remove("GAME").isError());
    CHECK(registry.remove("SUBGAME").isError());
}

TEST_CASE("layers: addLayer — vazio/duplicado rejeitados; ordem estável", "[scene][layers]")
{
    eng::scene::LayerRegistry registry;

    CHECK(registry.addLayer("").isError());
    CHECK(registry.addLayer("GAME").isError());  // colide com built-in

    REQUIRE(registry.addLayer("UI").ok());
    REQUIRE(registry.addLayer("LEVEL").ok());
    CHECK(registry.addLayer("UI").isError());  // duplicada

    const auto& definitions = registry.definitions();
    REQUIRE(definitions.size() == 4);
    CHECK(definitions[0].name == "GAME");     // built-ins primeiro
    CHECK(definitions[1].name == "SUBGAME");
    CHECK(definitions[2].name == "UI");       // ordem de adição
    CHECK(definitions[3].name == "LEVEL");

    // nova camada nasce com defaults
    CHECK(definitions[2].participation.update);
    CHECK(definitions[2].timeScale == 1.f);
}

TEST_CASE("layers: setParticipation/setTimeScale validam", "[scene][layers]")
{
    eng::scene::LayerRegistry registry;
    REQUIRE(registry.addLayer("UI").ok());

    eng::scene::LayerParticipation flags;
    flags.update = false;
    flags.render = true;
    REQUIRE(registry.setParticipation("UI", flags).ok());
    CHECK(registry.find("UI")->participation.update == false);
    CHECK(registry.find("UI")->participation.physics);  // não tocado
    CHECK(registry.find("UI")->participation.render);

    CHECK(registry.setParticipation("AUSENTE", flags).isError());

    REQUIRE(registry.setTimeScale("UI", 0.5f).ok());
    CHECK(registry.find("UI")->timeScale == approx(0.5f));
    CHECK(registry.setTimeScale("UI", -1.f).isError());   // negativo
    CHECK(registry.setTimeScale("UI", 2000.f).isError());  // fora do teto
    CHECK(registry.setTimeScale("AUSENTE", 1.f).isError());
}

// =============================================================================
// Scene: consultas por entidade
// =============================================================================

TEST_CASE("layers: sem LayerMember a entidade é GAME (default)", "[scene][layers]")
{
    eng::scene::Scene scene;
    const auto node = scene.createNode();

    CHECK(scene.layerOf(node) == "GAME");
    CHECK(scene.participatesIn(node, eng::scene::LayerStage::Update));
    CHECK(scene.participatesIn(node, eng::scene::LayerStage::Physics));
    CHECK(scene.participatesIn(node, eng::scene::LayerStage::Render));
    CHECK(scene.timeScaleOf(node) == approx(1.f));
}

TEST_CASE("layers: LayerMember direciona a consulta", "[scene][layers]")
{
    eng::scene::Scene scene;
    REQUIRE(scene.layers().addLayer("FX").ok());
    REQUIRE(scene.layers().addLayer("GHOST").ok());

    eng::scene::LayerParticipation off;
    off.update = false;
    off.physics = false;
    off.render = false;
    REQUIRE(scene.layers().setParticipation("GHOST", off).ok());
    REQUIRE(scene.layers().setTimeScale("FX", 0.25f).ok());

    const auto fx = scene.createNode();
    const auto ghost = scene.createNode();
    const auto game = scene.createNode();

    (void)scene.world().emplace<eng::scene::LayerMember>(
        fx, eng::scene::LayerMember{"FX"});
    (void)scene.world().emplace<eng::scene::LayerMember>(
        ghost, eng::scene::LayerMember{"GHOST"});

    CHECK(scene.layerOf(fx) == "FX");
    CHECK(scene.participatesIn(fx, eng::scene::LayerStage::Update));
    CHECK(scene.timeScaleOf(fx) == approx(0.25f));

    CHECK(scene.layerOf(ghost) == "GHOST");
    CHECK_FALSE(
        scene.participatesIn(ghost, eng::scene::LayerStage::Update));
    CHECK_FALSE(
        scene.participatesIn(ghost, eng::scene::LayerStage::Physics));
    CHECK_FALSE(
        scene.participatesIn(ghost, eng::scene::LayerStage::Render));

    CHECK(scene.layerOf(game) == "GAME");
    CHECK(scene.participatesIn(game, eng::scene::LayerStage::Physics));
}

TEST_CASE("layers: removeLayer rejeita camada em uso, aceita livre", "[scene][layers]")
{
    eng::scene::Scene scene;
    REQUIRE(scene.layers().addLayer("UI").ok());

    const auto node = scene.createNode();
    (void)scene.world().emplace<eng::scene::LayerMember>(
        node, eng::scene::LayerMember{"UI"});

    auto inUse = scene.removeLayer("UI");
    REQUIRE(inUse.isError());
    CHECK(inUse.error().message.find("usada por entidades") !=
          std::string::npos);

    // Remove o componente → camada livre.
    CHECK(scene.world().remove<eng::scene::LayerMember>(node));
    REQUIRE(scene.removeLayer("UI").ok());
    CHECK_FALSE(scene.layers().has("UI"));
    CHECK(scene.removeLayer("UI").isError());  // já não existe
}

// =============================================================================
// Serialização
// =============================================================================

TEST_CASE("layers: round-trip save/load preserva definições", "[scene][layers]")
{
    eng::scene::Scene scene;
    REQUIRE(scene.layers().addLayer("FX").ok());
    REQUIRE(scene.layers().setTimeScale("FX", 0.f).ok());  // pausada

    eng::scene::LayerParticipation subgame;
    subgame.update = false;
    subgame.physics = false;
    REQUIRE(scene.layers().setParticipation("SUBGAME", subgame).ok());

    const auto member = scene.createNode();
    (void)scene.world().emplace<eng::scene::LayerMember>(
        member, eng::scene::LayerMember{"FX"});

    const auto saved = eng::scene::SceneSerializer::save(scene);
    REQUIRE(saved.ok());

    eng::scene::Scene loaded;
    REQUIRE(
        eng::scene::SceneSerializer::load(loaded, saved.value()).ok());

    REQUIRE(loaded.layers().find("FX") != nullptr);
    CHECK(loaded.layers().find("FX")->timeScale == approx(0.f));
    REQUIRE(loaded.layers().find("SUBGAME") != nullptr);
    CHECK_FALSE(loaded.layers().find("SUBGAME")->participation.update);
    CHECK(loaded.layers().find("SUBGAME")->participation.render);

    // Membro preservado (componente LayerMember via catálogo)
    CHECK(loaded.world().componentCount<eng::scene::LayerMember>() == 1);
    CHECK(loaded.layerOf(member) == "FX");  // handle preservado (mesma ordem)
}

TEST_CASE("layers: cena sem a seção layers carrega com defaults (compat)", "[scene][layers]")
{
    // Arquivo da PRÉ-P0-5 não tem "layers" — parser ignora, defaults vivem.
    const std::string legacy = R"({
        "formatVersion": 1,
        "sceneEntityIds": [],
        "entities": []
    })";
    eng::scene::Scene scene;
    REQUIRE(eng::scene::SceneSerializer::load(scene, legacy).ok());
    CHECK(scene.layers().has("GAME"));
    CHECK(scene.layers().has("SUBGAME"));
    CHECK(scene.layers().definitions().size() == 2);
}

TEST_CASE("layers: LayerMember com camada não definida é ParseError", "[scene][layers]")
{
    eng::scene::Scene scene;
    const auto node = scene.createNode();
    (void)scene.world().emplace<eng::scene::LayerMember>(
        node, eng::scene::LayerMember{"NUNCA_DEFINIDA"});
    const auto saved = eng::scene::SceneSerializer::save(scene);
    REQUIRE(saved.ok());

    eng::scene::Scene loaded;
    const auto result =
        eng::scene::SceneSerializer::load(loaded, saved.value());
    REQUIRE(result.isError());
    CHECK(result.error().message.find("camada não definida") !=
          std::string::npos);
}
