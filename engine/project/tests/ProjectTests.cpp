#include <catch2/catch_test_macros.hpp>

#include <string>

#include "eng/fs/MemoryFileSystem.hpp"
#include "eng/project/Project.hpp"

namespace {

/// project.goni.json canônico de teste.
std::string validProjectText(const std::string& projectId)
{
    return "{\"assetRegistryPath\":\"asset_registry.json\",\"engineVersion\":"
           "\"0.3.0\",\"formatVersion\":1,\"name\":\"Meu Jogo\","
           "\"projectId\":\"" +
           projectId +
           "\",\"sceneRoots\":[\"assets/scenes\",\"assets/menus\"]}";
}

} // namespace

TEST_CASE("project: ProjectFile round-trip determinístico", "[project]")
{
    const eng::project::ProjectId id = eng::project::ProjectId::generate();
    const std::string text = validProjectText(id.toString());

    const auto parsed = eng::project::ProjectFile::parse(
        eng::fs::Path{"/games/jogo/project.goni.json"}, text);
    REQUIRE(parsed.ok());

    CHECK(parsed.value().config.projectId == id);
    CHECK(parsed.value().config.name == "Meu Jogo");
    CHECK(parsed.value().config.engineVersion ==
          eng::core::Version{0, 3, 0});
    CHECK(parsed.value().config.assetRegistryPath ==
          eng::fs::Path{"asset_registry.json"});
    REQUIRE(parsed.value().config.sceneRoots.size() == 2);
    CHECK(parsed.value().config.sceneRoots[1] ==
          eng::fs::Path{"assets/menus"});

    // Round-trip: serialize → mesmos bytes (determinismo)
    const auto serialized = parsed.value().serialize();
    REQUIRE(serialized.ok());
    const auto reparsed = eng::project::ProjectFile::parse(
        eng::fs::Path{"/games/jogo/project.goni.json"}, serialized.value());
    REQUIRE(reparsed.ok());
    CHECK(reparsed.value().serialize().value() == serialized.value());

    // readFrom/writeTo via FileSystem (MemoryFileSystem)
    eng::fs::MemoryFileSystem fs;
    REQUIRE(fs.mkdirs(eng::fs::Path{"/games/jogo"}));
    REQUIRE(parsed.value().writeTo(fs));
    const auto reread = eng::project::ProjectFile::readFrom(
        fs, eng::fs::Path{"/games/jogo/project.goni.json"});
    REQUIRE(reread.ok());
    CHECK(reread.value().config == parsed.value().config);
}

TEST_CASE("project: mover o arquivo de diretório resolve paths novos",
          "[project]")
{
    // Critério E: NENHUM absoluto persistido — o MESMO conteúdo resolve
    // raízes diferentes conforme o diretório do arquivo.
    const eng::project::ProjectId id = eng::project::ProjectId::generate();
    const std::string text = validProjectText(id.toString());

    const auto atA = eng::project::ProjectFile::parse(
        eng::fs::Path{"/games/alpha/project.goni.json"}, text);
    const auto atB = eng::project::ProjectFile::parse(
        eng::fs::Path{"/outro/lugar/project.goni.json"}, text);
    REQUIRE(atA.ok());
    REQUIRE(atB.ok());

    const eng::project::ProjectPaths pathsA = atA.value().paths();
    CHECK(pathsA.projectDir().str() == "/games/alpha");
    CHECK(pathsA.assetsRoot().str() == "/games/alpha/assets");
    CHECK(pathsA.cacheRoot().str() == "/games/alpha/cache");
    CHECK(pathsA.buildRoot().str() == "/games/alpha/build");
    CHECK(pathsA
              .sceneRoots(atA.value().config.sceneRoots)[0]
              .str() == "/games/alpha/assets/scenes");

    const eng::project::ProjectPaths pathsB = atB.value().paths();
    CHECK(pathsB.assetsRoot().str() == "/outro/lugar/assets");
    CHECK(pathsB.cacheRoot().str() == "/outro/lugar/cache");

    // O texto persistido NÃO contém caminho absoluto nenhum
    CHECK(text.find("/games") == std::string::npos);
    CHECK(text.find("/outro") == std::string::npos);
}

TEST_CASE("project: path absoluto é REJEITADO no parse", "[project]")
{
    using eng::core::StatusCode;

    const eng::project::ProjectId id = eng::project::ProjectId::generate();
    const std::string base = validProjectText(id.toString());

    // assetRegistryPath absoluto
    {
        std::string evil = base;
        evil.replace(evil.find("\"asset_registry.json\""),
                     std::string("\"asset_registry.json\"").size(),
                     "\"/etc/passwd\"");
        const auto r = eng::project::ProjectFile::parse(
            eng::fs::Path{"/p/project.goni.json"}, evil);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::InvalidArgument);
        CHECK(r.error().message.find("ABSOLUTO") != std::string::npos);
    }

    // sceneRoot absoluto
    {
        std::string evil = base;
        evil.replace(evil.find("\"assets/scenes\""),
                     std::string("\"assets/scenes\"").size(),
                     "\"/absoluto/scenes\"");
        const auto r = eng::project::ProjectFile::parse(
            eng::fs::Path{"/p/project.goni.json"}, evil);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::InvalidArgument);
    }
}

TEST_CASE("project: formatVersion futura e JSON corrompido → erros claros",
          "[project]")
{
    using eng::core::StatusCode;

    const eng::project::ProjectId id = eng::project::ProjectId::generate();
    const std::string base = validProjectText(id.toString());

    // versão maior que a suportada → NotSupported
    {
        std::string future = base;
        future.replace(future.find("\"formatVersion\":1"),
                       std::string("\"formatVersion\":1").size(),
                       "\"formatVersion\":42");
        const auto r = eng::project::ProjectFile::parse(
            eng::fs::Path{"/p/project.goni.json"}, future);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::NotSupported);
    }

    // corrompido
    for (const std::string_view broken :
         {"{", "não é json", "{\"formatVersion\":1}",
          "{\"formatVersion\":1,\"projectId\":\"xxx\",\"name\":\"n\","
          "\"engineVersion\":\"0.1\",\"assetRegistryPath\":\"a.json\"}"}) {
        const auto r = eng::project::ProjectFile::parse(
            eng::fs::Path{"/p/project.goni.json"}, broken);
        INFO("input: " << broken);
        CHECK(r.isError());
    }

    // engineVersion inválida (sufixo) → ParseError de Version
    {
        std::string bad = base;
        bad.replace(bad.find("\"0.3.0\""), std::string("\"0.3.0\"").size(),
                    "\"v0.3.0\"");
        const auto r = eng::project::ProjectFile::parse(
            eng::fs::Path{"/p/project.goni.json"}, bad);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::ParseError);
    }
}

TEST_CASE("project: systemDefaults via eng::platform", "[project]")
{
    const auto paths =
        eng::project::ProjectPaths::systemDefaults("eng-tests");
    CHECK_FALSE(paths.projectDir().isEmpty());
    // cacheRoot = projectDir/cache (composição visível do default)
    CHECK(paths.cacheRoot() ==
          (paths.projectDir() / eng::fs::Path{"cache"}).normalized());
}
