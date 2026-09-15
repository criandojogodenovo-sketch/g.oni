#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <unordered_set>

#include "eng/assets/Assets.hpp"
#include "eng/fs/MemoryFileSystem.hpp"

// =============================================================================
// AssetId — identidade UUIDv4 (critério C da missão)
// =============================================================================

TEST_CASE("assets: AssetId unicidade em 100k gerações (teste real)",
          "[assets]")
{
    std::unordered_set<eng::assets::AssetId> seen;
    seen.reserve(100000);
    for (int i = 0; i < 100000; ++i) {
        const eng::assets::AssetId id = eng::assets::AssetId::generate();
        CHECK_FALSE(id.isNil());
        CHECK(seen.insert(id).second); // colisão = falha imediata
    }
    CHECK(seen.size() == 100000);
}

TEST_CASE("assets: AssetId round-trip string e binário", "[assets]")
{
    const eng::assets::AssetId id = eng::assets::AssetId::generate();

    const auto byString = eng::assets::AssetId::fromString(id.toString());
    REQUIRE(byString.ok());
    CHECK(byString.value() == id);

    const auto byBytes = eng::assets::AssetId::fromBytesBE(id.toBytesBE());
    REQUIRE(byBytes.ok());
    CHECK(byBytes.value() == id);

    // Parse estrito propaga do Uuid128
    CHECK(eng::assets::AssetId::fromString("nao-e-uuid").isError());
    CHECK(eng::assets::AssetId::fromString("").isError());

    // Tipo FORTE: AssetId não se confunde com Uuid128 cru (compilação
    // garantiria; aqui validamos a ordem/igualdade entre ids distintos).
    const eng::assets::AssetId other = eng::assets::AssetId::generate();
    CHECK((id < other) != (other < id)); // ordem total consistente
}

// =============================================================================
// AssetRegistry — catálogo persistente determinístico
// =============================================================================

namespace {

eng::assets::AssetMeta makeMeta(const char* path,
                                eng::assets::AssetType type)
{
    eng::assets::AssetMeta meta;
    meta.id = eng::assets::AssetId::generate();
    meta.type = type;
    meta.sourcePath = eng::fs::Path{path};
    return meta;
}

} // namespace

TEST_CASE("assets: AssetRegistry round-trip JSON determinístico", "[assets]")
{
    eng::assets::AssetRegistry registry;
    eng::assets::AssetMeta a = makeMeta("scenes/main.json",
                                        eng::assets::AssetType::Scene);
    eng::assets::AssetMeta b = makeMeta("prefabs/caixa.json",
                                        eng::assets::AssetType::Prefab);
    b.size = 1234;
    REQUIRE(registry.upsert(a).ok());
    REQUIRE(registry.upsert(b).ok());
    CHECK(registry.size() == 2);

    const auto text = registry.serialize();
    REQUIRE(text.ok());

    // Determinismo: mesma entrada → mesmos bytes (ordem por AssetId).
    const auto again = registry.serialize();
    REQUIRE(again.ok());
    CHECK(text.value() == again.value());

    // Round-trip
    const auto loaded =
        eng::assets::AssetRegistry::deserialize(text.value());
    REQUIRE(loaded.ok());
    CHECK(loaded.value().size() == 2);
    const auto* foundA = loaded.value().find(a.id);
    REQUIRE(foundA != nullptr);
    CHECK(foundA->type == eng::assets::AssetType::Scene);
    CHECK(foundA->sourcePath == eng::fs::Path{"scenes/main.json"});
    const auto* foundB = loaded.value().find(b.id);
    REQUIRE(foundB != nullptr);
    CHECK(foundB->size.has_value());
    CHECK(*foundB->size == 1234);

    // upsert substitui (mesmo id, novo path) — base da renomeação
    eng::assets::AssetMeta renamed = a;
    renamed.sourcePath = eng::fs::Path{"scenes/renomeada.json"};
    REQUIRE(registry.upsert(renamed).ok());
    CHECK(registry.size() == 2); // substituiu, não duplicou
    CHECK(registry.find(a.id)->sourcePath ==
          eng::fs::Path{"scenes/renomeada.json"});
}

TEST_CASE("assets: renomear asset não invalida referências", "[assets]")
{
    eng::assets::AssetRegistry registry;
    const eng::assets::AssetMeta meta =
        makeMeta("scenes/antes.json", eng::assets::AssetType::Scene);
    const eng::assets::AssetId id = meta.id; // referência "guardada" por um
                                             // componente de cena
    REQUIRE(registry.upsert(meta).ok());

    // Arquivo movido no disco + registry atualizado
    eng::assets::AssetMeta moved = meta;
    moved.sourcePath = eng::fs::Path{"scenes/depois.json"};
    REQUIRE(registry.upsert(moved).ok());

    // A referência por AssetId continua resolvendo (critério C)
    eng::fs::MemoryFileSystem fs;
    REQUIRE(fs.mkdirs(eng::fs::Path{"/proj/assets/scenes"}));
    REQUIRE(fs.writeAllText(eng::fs::Path{"/proj/assets/scenes/depois.json"},
                            "{}"));
    eng::assets::AssetResolver resolver(registry,
                                        eng::fs::Path{"/proj/assets"});
    const auto resolved = resolver.resolve(id);
    REQUIRE(resolved.ok());
    CHECK(resolved.value().str() == "/proj/assets/scenes/depois.json");
}

TEST_CASE("assets: AssetRegistry rejeita entradas inválidas", "[assets]")
{
    using eng::core::StatusCode;

    // id nulo no upsert → InvalidArgument
    {
        eng::assets::AssetRegistry registry;
        eng::assets::AssetMeta meta; // id nulo
        meta.sourcePath = eng::fs::Path{"x.json"};
        const auto r = registry.upsert(meta);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::InvalidArgument);
    }

    // remove de ausente → NotFound
    {
        eng::assets::AssetRegistry registry;
        const auto r = registry.remove(eng::assets::AssetId::generate());
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::NotFound);
    }

    // JSON de registry corrompido/incompleto → erros claros
    const auto check = [](std::string_view text, const char* why) {
        const auto r = eng::assets::AssetRegistry::deserialize(text);
        INFO(why << " — input: " << text);
        REQUIRE(r.isError());
        CHECK(r.error().code == eng::core::StatusCode::ParseError);
    };

    check("{\"formatVersion\":1,\"assets\":[{\"type\":\"Scene\"}]}",
          "entrada sem id");
    check("{\"formatVersion\":1,\"assets\":[{\"id\":\"xxxx\",\"type\":"
          "\"Scene\",\"sourcePath\":\"a\"}]}",
          "id não canônico");
    check("{\"formatVersion\":1,\"assets\":[{\"id\":\"00112233-4455-4677-"
          "8899-aabbccddeeff\",\"type\":\"Picles\",\"sourcePath\":\"a\"}]}",
          "tipo desconhecido");
    check("{\"formatVersion\":1,\"assets\":[{\"id\":\"00112233-4455-4677-"
          "8899-aabbccddeeff\",\"type\":\"Scene\"}]}",
          "sem sourcePath");
    check("{", "json quebrado");

    // formatVersion futura → NotSupported (não tenta parsear)
    {
        const auto r = eng::assets::AssetRegistry::deserialize(
            "{\"formatVersion\":99,\"assets\":[]}");
        REQUIRE(r.isError());
        CHECK(r.error().code == eng::core::StatusCode::NotSupported);
    }
}

// =============================================================================
// AssetResolver — defesa anti-traversal (critério F)
// =============================================================================

TEST_CASE("assets: resolver encontra asset e rejeita traversal", "[assets]")
{
    using eng::core::StatusCode;

    eng::assets::AssetRegistry registry;
    const eng::assets::AssetMeta good =
        makeMeta("scenes/ok.json", eng::assets::AssetType::Scene);
    REQUIRE(registry.upsert(good).ok());

    eng::fs::MemoryFileSystem fs;
    REQUIRE(fs.mkdirs(eng::fs::Path{"/proj/assets/scenes"}));
    REQUIRE(fs.writeAllText(eng::fs::Path{"/proj/assets/scenes/ok.json"},
                            "{\"cena\":true}"));

    eng::assets::AssetResolver resolver(registry,
                                        eng::fs::Path{"/proj/assets"});

    // encontrado
    {
        const auto resolved = resolver.resolve(good.id);
        REQUIRE(resolved.ok());
        CHECK(resolved.value().str() == "/proj/assets/scenes/ok.json");

        const auto bytes = resolver.read(good.id, fs);
        REQUIRE(bytes.ok());
        CHECK(bytes.value().size() == 13);
    }

    // id ausente → NotFound claro
    {
        const auto r = resolver.resolve(eng::assets::AssetId::generate());
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::NotFound);
    }

    // traversal: registry forjado com ".." que escapa da raiz
    {
        eng::assets::AssetRegistry evil;
        eng::assets::AssetMeta escape;
        escape.id = eng::assets::AssetId::generate();
        escape.type = eng::assets::AssetType::Json;
        escape.sourcePath = eng::fs::Path{"../../etc/passwd"};
        REQUIRE(evil.upsert(escape).ok());

        eng::assets::AssetResolver evilResolver(evil,
                                                eng::fs::Path{"/proj/assets"});
        const auto r = evilResolver.resolve(escape.id);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::InvalidArgument);
        CHECK(r.error().message.find("escapa") != std::string::npos);
    }

    // path absoluto no sourcePath → rejeitado
    {
        eng::assets::AssetRegistry absRegistry;
        eng::assets::AssetMeta absolute;
        absolute.id = eng::assets::AssetId::generate();
        absolute.type = eng::assets::AssetType::Json;
        absolute.sourcePath = eng::fs::Path{"/etc/passwd"};
        REQUIRE(absRegistry.upsert(absolute).ok());

        eng::assets::AssetResolver absResolver(absRegistry,
                                               eng::fs::Path{"/proj/assets"});
        const auto r = absResolver.resolve(absolute.id);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::InvalidArgument);
        CHECK(r.error().message.find("absoluto") != std::string::npos);
    }
}

// =============================================================================
// AssetManager — cache, load/unload, type-guard sem RTTI
// =============================================================================

TEST_CASE("assets: manager load/cache/unload com JsonAssetLoader",
          "[assets]")
{
    using eng::core::StatusCode;

    eng::assets::AssetRegistry registry;
    const eng::assets::AssetMeta sceneMeta =
        makeMeta("scenes/main.json", eng::assets::AssetType::Scene);
    REQUIRE(registry.upsert(sceneMeta).ok());

    eng::fs::MemoryFileSystem fs;
    REQUIRE(fs.mkdirs(eng::fs::Path{"/proj/assets/scenes"}));
    REQUIRE(fs.writeAllText(eng::fs::Path{"/proj/assets/scenes/main.json"},
                            "{\"formatVersion\":1,\"entities\":[]}"));

    eng::assets::AssetResolver resolver(registry,
                                        eng::fs::Path{"/proj/assets"});
    eng::assets::AssetManager manager(registry, resolver, fs);
    manager.registerLoader<eng::serial::JsonValue>(
        std::make_shared<eng::assets::JsonAssetLoader>());

    // load → handle válido com o JSON parseado
    auto handle = manager.load<eng::serial::JsonValue>(sceneMeta.id);
    REQUIRE(handle.ok());
    CHECK(handle.value().id() == sceneMeta.id);
    CHECK(handle.value()->isObject());
    CHECK(handle.value()->find("formatVersion").has_value());

    // cache: segundo load devolve o MESMO objeto (sem reler)
    auto again = manager.load<eng::serial::JsonValue>(sceneMeta.id);
    REQUIRE(again.ok());
    CHECK(again.value().get() == handle.value().get());
    CHECK(manager.loadedCount() == 1);

    // asset inexistente → erro claro
    {
        const auto r =
            manager.load<eng::serial::JsonValue>(
                eng::assets::AssetId::generate());
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::NotFound);
        CHECK(r.error().message.find("registry") != std::string::npos);
    }

    // getLoaded com tipo C++ ERRADO → nullopt (guarda sem RTTI)
    struct NotRegistered {};
    CHECK_FALSE(
        manager.getLoaded<NotRegistered>(sceneMeta.id).has_value());

    // unload remove do cache; handles vivos seguram os dados
    manager.unload(sceneMeta.id);
    CHECK(manager.loadedCount() == 0);
    CHECK_FALSE(
        manager.getLoaded<eng::serial::JsonValue>(sceneMeta.id).has_value());
    CHECK(handle.value()->isObject()); // handle antigo AINDA válido

    // reload volta a funcionar (novo objeto)
    auto third = manager.load<eng::serial::JsonValue>(sceneMeta.id);
    REQUIRE(third.ok());
    CHECK(third.value().get() != handle.value().get());
}

TEST_CASE("assets: manager rejeita AssetType sem loader suportado",
          "[assets]")
{
    using eng::core::StatusCode;

    eng::assets::AssetRegistry registry;
    const eng::assets::AssetMeta texture =
        makeMeta("tex/wood.png", eng::assets::AssetType::Texture);
    REQUIRE(registry.upsert(texture).ok()); // Texture: RESERVADO, sem loader

    eng::fs::MemoryFileSystem fs;
    eng::assets::AssetResolver resolver(registry,
                                        eng::fs::Path{"/proj/assets"});
    eng::assets::AssetManager manager(registry, resolver, fs);
    manager.registerLoader<eng::serial::JsonValue>(
        std::make_shared<eng::assets::JsonAssetLoader>());

    const auto r = manager.load<eng::serial::JsonValue>(texture.id);
    REQUIRE(r.isError());
    CHECK(r.error().code == StatusCode::NotSupported);
    CHECK(r.error().message.find("Texture") != std::string::npos);
}
