#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "eng/assets/Assets.hpp"
#include "eng/fs/MemoryFileSystem.hpp"
#include "eng/math/Transform.hpp"
#include "eng/project/Project.hpp"
#include "eng/scene/Scene.hpp"
#include "eng/scene/SceneIdentity.hpp"
#include "eng/scene/SceneSerializer.hpp"
#include "eng/serial/Serial.hpp"

// =============================================================================
// Componente de gameplay para o e2e (registro em init estática — ADR-033)
// =============================================================================

namespace {

struct SpriteRef {
    eng::assets::AssetId texture;
    float opacity = 1.0f;
};

ENG_REFLECT_BEGIN(SpriteRef)
    ENG_REFLECT_FIELD_AS(texture, "eng::assets::AssetId")
    ENG_REFLECT_FIELD(opacity)
ENG_REFLECT_END()

const bool spriteRefRegistered = [] {
    (void)eng::scene::SceneSerializer::registerComponentType<SpriteRef>(
        "SpriteRef");
    return true;
}();

/// Estado completo do projeto de teste em um MemoryFileSystem.
struct TestProject {
    eng::fs::MemoryFileSystem fs;
    const eng::fs::Path root{"/jogo"};
    eng::assets::AssetId sceneAssetId;
    std::string originalSceneText;
};

/// Monta: project.goni.json + asset_registry.json + cena com hierarquia e
/// componente com referência de asset (válida).
TestProject buildProject()
{
    TestProject project;

    REQUIRE(project.fs.mkdirs(project.root / eng::fs::Path{"assets/scenes"}));

    // 1. Cena canônica (bytes de entrada do e2e)
    eng::scene::Scene scene;
    const auto root = scene.createNode();
    const auto child = scene.createNode();
    REQUIRE(scene.attach(child, root));
    scene.localTransform(child)->position = {4.0f, -1.5f, 2.0f};

    const auto pre = eng::scene::SceneSerializer::save(scene);
    REQUIRE(pre.ok());

    SpriteRef sprite;
    sprite.texture = eng::assets::AssetId::generate();
    sprite.opacity = 0.5f;
    (void)scene.world().emplace<SpriteRef>(child, sprite);

    const auto saved = eng::scene::SceneSerializer::save(scene);
    REQUIRE(saved.ok());
    project.originalSceneText = saved.value();
    project.sceneAssetId = sprite.texture == sprite.texture
                               ? eng::assets::AssetId::generate()
                               : eng::assets::AssetId::generate();

    REQUIRE(project.fs.writeAllText(
        project.root / eng::fs::Path{"assets/scenes/main.goni.scene.json"},
        project.originalSceneText));

    // 2. Registry com a cena catalogada
    eng::assets::AssetRegistry registry;
    eng::assets::AssetMeta sceneMeta;
    sceneMeta.id = project.sceneAssetId;
    sceneMeta.type = eng::assets::AssetType::Scene;
    sceneMeta.sourcePath = eng::fs::Path{"scenes/main.goni.scene.json"};
    sceneMeta.size = project.originalSceneText.size();
    REQUIRE(registry.upsert(sceneMeta).ok());
    const auto registryText = registry.serialize();
    REQUIRE(registryText.ok());
    REQUIRE(project.fs.writeAllText(
        project.root / eng::fs::Path{"asset_registry.json"},
        registryText.value()));

    // 3. project.goni.json
    eng::project::ProjectConfig config;
    config.projectId = eng::project::ProjectId::generate();
    config.name = "Jogo de Teste";
    config.engineVersion = eng::core::Version{0, 3, 0};
    config.assetRegistryPath = eng::fs::Path{"asset_registry.json"};
    config.sceneRoots = {eng::fs::Path{"assets/scenes"}};

    eng::project::ProjectFile file;
    file.config = config;
    file.filePath = project.root / eng::fs::Path{"project.goni.json"};
    REQUIRE(file.writeTo(project.fs));

    return project;
}

} // namespace

// =============================================================================
// Pipeline completo (missão §2.8): project → paths → registry → resolver →
// manager → cena → ECS → reserialize == entrada
// =============================================================================

TEST_CASE("e2e: pipeline completo com bytes idênticos", "[integration]")
{
    TestProject project = buildProject();

    // 1. project file
    const auto file = eng::project::ProjectFile::readFrom(
        project.fs, project.root / eng::fs::Path{"project.goni.json"});
    REQUIRE(file.ok());

    // 2. paths resolvidos RELATIVOS ao diretório do arquivo
    const eng::project::ProjectPaths paths = file.value().paths();
    CHECK(paths.assetsRoot().str() == "/jogo/assets");

    // 3. registry (lido do path declarado no project)
    const auto registryText = project.fs.readAllText(
        file.value().paths().resolve(file.value().config.assetRegistryPath));
    REQUIRE(registryText.ok());
    const auto registry =
        eng::assets::AssetRegistry::deserialize(registryText.value());
    REQUIRE(registry.ok());
    REQUIRE(registry.value().size() == 1);

    // 4. resolver + manager + loader
    eng::assets::AssetResolver resolver(
        registry.value(), paths.assetsRoot());
    eng::assets::AssetManager manager(registry.value(), resolver,
                                      project.fs);
    manager.registerLoader<eng::serial::JsonValue>(
        std::make_shared<eng::assets::JsonAssetLoader>());

    const auto handle =
        manager.load<eng::serial::JsonValue>(project.sceneAssetId);
    REQUIRE(handle.ok());
    CHECK(manager.loadedCount() == 1);

    // 5. cena reconstruída a partir do JSON carregado
    eng::scene::Scene loaded;
    REQUIRE(eng::scene::SceneSerializer::load(
                loaded, eng::serial::dumpJson(*handle.value()))
                .ok());
    REQUIRE(loaded.nodeCount() == 2);

    bool sawSprite = false;
    loaded.world().each<SpriteRef>(
        [&](eng::ecs::Entity, const SpriteRef& sprite) {
            sawSprite = true;
            CHECK(sprite.opacity == 0.5f);
        });
    CHECK(sawSprite);

    // 6. RESERIALIZE == ENTRADA (critério B/D, byte a byte)
    const auto resaved = eng::scene::SceneSerializer::save(loaded);
    REQUIRE(resaved.ok());
    CHECK(resaved.value() == project.originalSceneText);

    // 7. mutação leve muda os bytes — e o novo estado round-tripa
    loaded.world().each<eng::math::Transform>(
        [&](eng::ecs::Entity, eng::math::Transform& transform) {
            transform.position.y = 99.0f;
        });
    const auto mutated = eng::scene::SceneSerializer::save(loaded);
    REQUIRE(mutated.ok());
    CHECK(mutated.value() != project.originalSceneText);

    eng::scene::Scene reloaded;
    REQUIRE(eng::scene::SceneSerializer::load(reloaded, mutated.value()).ok());
    const auto again = eng::scene::SceneSerializer::save(reloaded);
    REQUIRE(again.ok());
    CHECK(again.value() == mutated.value());

    // 8. unload/getLoaded pela via do manager
    manager.unload(project.sceneAssetId);
    CHECK_FALSE(manager.getLoaded<eng::serial::JsonValue>(
                     project.sceneAssetId)
                    .has_value());
}

// =============================================================================
// Bateria de corrupção: cada estágio recebe input ruim → erro claro, sem
// crash, sem UB (missão §2.8)
// =============================================================================

TEST_CASE("e2e: corrupção por estágio → erros claros", "[integration]")
{
    using eng::core::StatusCode;

    TestProject project = buildProject();
    const eng::fs::Path projectFile =
        project.root / eng::fs::Path{"project.goni.json"};

    SECTION("project file corrompido")
    {
        REQUIRE(project.fs.writeAllText(projectFile, "{ quebrado"));
        const auto r = eng::project::ProjectFile::readFrom(project.fs,
                                                           projectFile);
        CHECK(r.isError());
        CHECK(r.error().code == StatusCode::ParseError);
    }

    SECTION("project file com versão futura")
    {
        REQUIRE(project.fs.writeAllText(
            projectFile,
            "{\"assetRegistryPath\":\"a.json\",\"engineVersion\":\"0.3.0\","
            "\"formatVersion\":42,\"name\":\"n\",\"projectId\":\"00112233-"
            "4455-4677-8899-aabbccddeeff\",\"sceneRoots\":[]}"));
        const auto r = eng::project::ProjectFile::readFrom(project.fs,
                                                           projectFile);
        CHECK(r.isError());
        CHECK(r.error().code == StatusCode::NotSupported);
    }

    SECTION("registry corrompido")
    {
        REQUIRE(project.fs.writeAllText(
            project.root / eng::fs::Path{"asset_registry.json"}, "["));
        const auto r = eng::assets::AssetRegistry::deserialize(
            project.fs
                .readAllText(project.root /
                             eng::fs::Path{"asset_registry.json"})
                .value());
        CHECK(r.isError());
    }

    SECTION("asset ausente do registry → load com erro claro")
    {
        eng::assets::AssetRegistry empty;
        const auto emptyText = empty.serialize();
        REQUIRE(emptyText.ok());
        REQUIRE(project.fs.writeAllText(
            project.root / eng::fs::Path{"asset_registry.json"},
            emptyText.value()));

        const auto registry =
            eng::assets::AssetRegistry::deserialize(emptyText.value());
        REQUIRE(registry.ok());
        eng::assets::AssetResolver resolver(registry.value(),
                                            eng::fs::Path{"/jogo/assets"});
        eng::assets::AssetManager manager(registry.value(), resolver,
                                          project.fs);
        manager.registerLoader<eng::serial::JsonValue>(
            std::make_shared<eng::assets::JsonAssetLoader>());

        const auto r =
            manager.load<eng::serial::JsonValue>(project.sceneAssetId);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::NotFound);
    }

    SECTION("cena corrompida no disco → parse claro")
    {
        REQUIRE(project.fs.writeAllText(
            project.root / eng::fs::Path{"assets/scenes/main.goni.scene.json"},
            "{\"formatVersion\":1,\"entities\": QUE"));
        const auto registryText = project.fs.readAllText(
            project.root / eng::fs::Path{"asset_registry.json"});
        const auto registry =
            eng::assets::AssetRegistry::deserialize(registryText.value());
        REQUIRE(registry.ok());
        eng::assets::AssetResolver resolver(registry.value(),
                                            eng::fs::Path{"/jogo/assets"});
        eng::assets::AssetManager manager(registry.value(), resolver,
                                          project.fs);
        manager.registerLoader<eng::serial::JsonValue>(
            std::make_shared<eng::assets::JsonAssetLoader>());
        const auto r =
            manager.load<eng::serial::JsonValue>(project.sceneAssetId);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::ParseError);
    }

    SECTION("traversal no registry forjado → rejeitado no resolve")
    {
        eng::assets::AssetRegistry evil;
        eng::assets::AssetMeta escape;
        escape.id = eng::assets::AssetId::generate();
        escape.type = eng::assets::AssetType::Json;
        escape.sourcePath = eng::fs::Path{"../../../secrets.txt"};
        REQUIRE(evil.upsert(escape).ok());
        eng::assets::AssetResolver resolver(evil,
                                            eng::fs::Path{"/jogo/assets"});
        const auto r = resolver.resolve(escape.id);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::InvalidArgument);
    }

    SECTION("envelope binário: CRC/magic/versão (formato futuro de cache)")
    {
        const std::string payload = project.originalSceneText;
        const std::span<const std::byte> bytes{
            reinterpret_cast<const std::byte*>(payload.data()),
            payload.size()};
        const auto envelope =
            eng::serial::encodeEnvelope(1, 1, bytes);
        REQUIRE(eng::serial::decodeEnvelope(envelope).ok());

        // CRC corrompido
        {
            std::vector<std::byte> corrupt = envelope;
            corrupt[corrupt.size() / 2] = std::byte{0x00};
            const auto r = eng::serial::decodeEnvelope(corrupt);
            REQUIRE(r.isError());
            CHECK(r.error().code == StatusCode::ParseError);
        }
        // magic errado
        {
            std::vector<std::byte> corrupt = envelope;
            corrupt[0] = std::byte{'X'};
            const auto r = eng::serial::decodeEnvelope(corrupt);
            REQUIRE(r.isError());
        }
        // payload do envelope decodificado volta a ser a cena (round-trip
        // do contêiner futuro de cache — ADR-029/030)
        {
            const auto decoded = eng::serial::decodeEnvelope(envelope);
            REQUIRE(decoded.ok());
            const std::string text(
                reinterpret_cast<const char*>(decoded.value().payload.data()),
                decoded.value().payload.size());
            CHECK(text == project.originalSceneText);
        }
    }
}
