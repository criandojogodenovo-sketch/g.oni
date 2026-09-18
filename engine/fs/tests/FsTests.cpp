#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "eng/fs/Fs.hpp"

// =============================================================================
// Path — unidade pura
// =============================================================================

TEST_CASE("fs: Path composição e decomposição", "[fs]")
{
    const eng::fs::Path p{"/data/scenes/level.goni.json"};

    CHECK(p.str() == "/data/scenes/level.goni.json");
    CHECK(p.parent().str() == "/data/scenes");
    CHECK(p.filename().str() == "level.goni.json");
    CHECK(p.stem().str() == "level.goni"); // stem remove só a ÚLTIMA extensão
    CHECK(p.extension().str() == ".json");
    CHECK(p.isAbsolute());
    CHECK_FALSE(p.isEmpty());

    const eng::fs::Path rel{"scenes"};
    CHECK_FALSE(rel.isAbsolute());
    CHECK(rel.parent().str().empty()); // pai de relativo simples é vazio
    CHECK(rel / eng::fs::Path{"level.json"} ==
          eng::fs::Path{"scenes/level.json"});
}

TEST_CASE("fs: Path normalização e validade", "[fs]")
{
    CHECK(eng::fs::Path{"a/./b/../c"}.normalized().str() == "a/c");
    CHECK(eng::fs::Path{"a//b"}.normalized().str() == "a/b");
    CHECK(eng::fs::Path{"../b"}.normalized().str() == "../b"); // escape preservado
    CHECK(eng::fs::Path{""}.isEmpty());
    CHECK_FALSE(eng::fs::Path{""}.valid());
    CHECK_FALSE(eng::fs::Path{std::string_view("a\0b", 3)}.valid());
    CHECK(eng::fs::Path{"ok.txt"}.valid());
}

TEST_CASE("fs: Path isWithin rejeita escape por componente", "[fs]")
{
    const eng::fs::Path root{"/proj/assets"};

    CHECK(eng::fs::Path{"/proj/assets"}.isWithin(root));
    CHECK(eng::fs::Path{"/proj/assets/scenes/a.json"}.isWithin(root));
    CHECK_FALSE(eng::fs::Path{"/proj/assets-base"}.isWithin(root));
    CHECK_FALSE(eng::fs::Path{"/proj"}.isWithin(root));
    CHECK_FALSE(eng::fs::Path{"/etc/passwd"}.isWithin(root));
    // Traversal clássico: normaliza para fora da raiz.
    CHECK_FALSE(eng::fs::Path{"/proj/assets/../secrets.txt"}.isWithin(root));
    CHECK(eng::fs::Path{"/proj/assets/../assets/x"}.isWithin(root));
}

// =============================================================================
// Memória de massa comum: exercita o CONTRATO em ambas as implementações
// (paridade Memory x Native é requisito — ADR-027).
// =============================================================================

namespace {

constexpr std::byte operator""_b(unsigned long long v)
{
    return static_cast<std::byte>(v);
}

} // namespace

TEST_CASE("fs: contrato básico (memory)", "[fs]")
{
    eng::fs::MemoryFileSystem fs;
    const auto root = eng::fs::Path{"/mem"};

    REQUIRE(fs.mkdirs(root / eng::fs::Path{"scenes"}));
    REQUIRE(fs.mkdirs(root / eng::fs::Path{"cache"}));

    // exists
    {
        auto r = fs.exists(root / eng::fs::Path{"scenes"});
        REQUIRE(r.ok());
        CHECK(r.value());
        auto missing = fs.exists(root / eng::fs::Path{"nao-existe"});
        REQUIRE(missing.ok());
        CHECK_FALSE(missing.value());
    }

    // write/read texto e bytes
    {
        REQUIRE(fs.writeAllText(root / eng::fs::Path{"scenes/a.json"}, "{}"));
        auto text = fs.readAllText(root / eng::fs::Path{"scenes/a.json"});
        REQUIRE(text.ok());
        CHECK(text.value() == "{}");

        const std::vector<std::byte> payload{0x00_b, 0x01_b, 0xFF_b};
        REQUIRE(
            fs.writeAllBytes(root / eng::fs::Path{"cache/blob.bin"}, payload));
        auto bytes = fs.readAllBytes(root / eng::fs::Path{"cache/blob.bin"});
        REQUIRE(bytes.ok());
        CHECK(bytes.value() == payload);
    }

    // arquivo vazio
    {
        REQUIRE(fs.writeAllText(root / eng::fs::Path{"cache/empty"}, ""));
        auto bytes = fs.readAllBytes(root / eng::fs::Path{"cache/empty"});
        REQUIRE(bytes.ok());
        CHECK(bytes.value().empty());
    }

    // remove
    {
        auto r = fs.remove(root / eng::fs::Path{"scenes/a.json"});
        REQUIRE(r.ok());
        CHECK(r.value());
        auto again = fs.remove(root / eng::fs::Path{"scenes/a.json"});
        REQUIRE(again.ok());
        CHECK_FALSE(again.value()); // já ausente: no-op
    }

    // rename de arquivo
    {
        REQUIRE(fs.writeAllText(root / eng::fs::Path{"scenes/b.json"}, "x"));
        REQUIRE(fs.rename(root / eng::fs::Path{"scenes/b.json"},
                          root / eng::fs::Path{"scenes/c.json"}));
        auto moved = fs.readAllText(root / eng::fs::Path{"scenes/c.json"});
        REQUIRE(moved.ok());
        CHECK(moved.value() == "x");
        auto old = fs.exists(root / eng::fs::Path{"scenes/b.json"});
        REQUIRE(old.ok());
        CHECK_FALSE(old.value());
    }

    // rename de diretório (árvore inteira)
    {
        REQUIRE(fs.rename(root / eng::fs::Path{"scenes"},
                          root / eng::fs::Path{"cenas"}));
        auto moved = fs.readAllText(root / eng::fs::Path{"cenas/c.json"});
        REQUIRE(moved.ok());
        CHECK(moved.value() == "x");
    }

    // list raso e recursivo
    {
        REQUIRE(fs.mkdirs(root / eng::fs::Path{"cenas/sub"}));
        REQUIRE(fs.writeAllText(root / eng::fs::Path{"cenas/sub/d.json"}, "d"));

        auto flat = fs.list(root / eng::fs::Path{"cenas"}, false);
        REQUIRE(flat.ok());
        REQUIRE(flat.value().size() == 2);
        CHECK(flat.value()[0].path.str() == "/mem/cenas/c.json");
        CHECK_FALSE(flat.value()[0].isDirectory);
        CHECK(flat.value()[1].path.str() == "/mem/cenas/sub");
        CHECK(flat.value()[1].isDirectory);

        auto deep = fs.list(root / eng::fs::Path{"cenas"}, true);
        REQUIRE(deep.ok());
        REQUIRE(deep.value().size() == 3);
        CHECK(deep.value()[2].path.str() == "/mem/cenas/sub/d.json");
    }

    // erros: ausente / path inválido / list em arquivo
    {
        auto missing = fs.readAllText(root / eng::fs::Path{"ghost"});
        REQUIRE(missing.isError());
        CHECK(missing.error().code == eng::core::StatusCode::NotFound);

        auto invalid = fs.readAllText(eng::fs::Path{""});
        REQUIRE(invalid.isError());
        CHECK(invalid.error().code == eng::core::StatusCode::InvalidArgument);

        auto listFile = fs.list(root / eng::fs::Path{"cenas/c.json"}, false);
        REQUIRE(listFile.isError());
        CHECK(listFile.error().code == eng::core::StatusCode::NotFound);
    }

    // writeAll NÃO cria pais (paridade com native)
    {
        auto r = fs.writeAllText(root / eng::fs::Path{"novo/dir/x.txt"}, "x");
        REQUIRE(r.isError());
        CHECK(r.error().code == eng::core::StatusCode::NotFound);
    }

    // remove de diretório não-vazio: false, sem erro
    {
        auto r = fs.remove(root / eng::fs::Path{"cenas"});
        REQUIRE(r.ok());
        CHECK_FALSE(r.value());
        auto still = fs.exists(root / eng::fs::Path{"cenas"});
        REQUIRE(still.ok());
        CHECK(still.value());
    }

    // mkdirs idempotente / sobre arquivo → erro
    {
        REQUIRE(fs.mkdirs(root / eng::fs::Path{"cenas"}));
        auto r = fs.mkdirs(root / eng::fs::Path{"cenas/c.json"});
        REQUIRE(r.isError());
        CHECK(r.error().code == eng::core::StatusCode::IOError);
    }
}

TEST_CASE("fs: round-trip memory (recria estado por bytes)", "[fs]")
{
    eng::fs::MemoryFileSystem fs;
    const auto root = eng::fs::Path{"/proj"};

    REQUIRE(fs.mkdirs(root / eng::fs::Path{"a/b"}));
    REQUIRE(fs.writeAllText(root / eng::fs::Path{"a/b/f1"}, "um"));
    REQUIRE(fs.writeAllText(root / eng::fs::Path{"a/f2"}, "dois"));

    eng::fs::MemoryFileSystem clone;
    REQUIRE(clone.mkdirs(root));
    auto entries = fs.list(root, true);
    REQUIRE(entries.ok());
    for (const auto& entry : entries.value()) {
        if (entry.isDirectory) {
            REQUIRE(clone.mkdirs(entry.path));
        } else {
            auto data = fs.readAllBytes(entry.path);
            REQUIRE(data.ok());
            REQUIRE(clone.writeAllBytes(entry.path, data.value()));
        }
    }

    auto original = fs.readAllText(root / eng::fs::Path{"a/b/f1"});
    auto copied = clone.readAllText(root / eng::fs::Path{"a/b/f1"});
    REQUIRE(original.ok());
    REQUIRE(copied.ok());
    CHECK(original.value() == copied.value());
}

// =============================================================================
// Native — tmpdir real com fixture de limpeza
// =============================================================================

namespace {

class TmpDir {
public:
    TmpDir()
    {
        std::error_code ec;
        auto base = std::filesystem::temp_directory_path(ec);
        if (ec || base.empty()) {
            base = std::filesystem::path("/tmp");
        }
        std::filesystem::path candidate;
        do {
            candidate = base / ("eng_fs_test_" + std::to_string(++counter_));
        } while (std::filesystem::exists(candidate, ec));
        std::filesystem::create_directories(candidate, ec);
        dir_ = candidate;
    }

    ~TmpDir() { cleanup(); }

    TmpDir(const TmpDir&) = delete;
    TmpDir& operator=(const TmpDir&) = delete;

    [[nodiscard]] eng::fs::Path path() const
    {
        return eng::fs::Path{dir_.generic_string()};
    }

    void cleanup()
    {
        if (!dir_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
            dir_.clear();
        }
    }

private:
    static std::uint64_t counter_;
    std::filesystem::path dir_;
};

std::uint64_t TmpDir::counter_ = 0;

} // namespace

TEST_CASE("fs: NativeFileSystem round-trip em tmpdir", "[fs][native]")
{
    TmpDir tmp;
    eng::fs::NativeFileSystem fs;
    const auto root = tmp.path();

    REQUIRE(fs.mkdirs(root / eng::fs::Path{"scenes"}));

    // write/read texto
    REQUIRE(fs.writeAllText(root / eng::fs::Path{"scenes/scene.json"},
                            "{\"a\":1}"));
    {
        auto text = fs.readAllText(root / eng::fs::Path{"scenes/scene.json"});
        REQUIRE(text.ok());
        CHECK(text.value() == "{\"a\":1}");
    }

    // bytes binários com NUL e 0xFF
    {
        const std::vector<std::byte> payload{0x00_b, 0xFF_b, 0x42_b, 0x00_b};
        REQUIRE(fs.writeAllBytes(root / eng::fs::Path{"scenes/blob.bin"},
                                 payload));
        auto back = fs.readAllBytes(root / eng::fs::Path{"scenes/blob.bin"});
        REQUIRE(back.ok());
        CHECK(back.value() == payload);
    }

    // arquivo vazio
    {
        REQUIRE(fs.writeAllText(root / eng::fs::Path{"empty.txt"}, ""));
        auto bytes = fs.readAllBytes(root / eng::fs::Path{"empty.txt"});
        REQUIRE(bytes.ok());
        CHECK(bytes.value().empty());
    }

    // rename
    {
        REQUIRE(fs.rename(root / eng::fs::Path{"scenes/scene.json"},
                          root / eng::fs::Path{"scenes/renamed.json"}));
        auto gone = fs.exists(root / eng::fs::Path{"scenes/scene.json"});
        REQUIRE(gone.ok());
        CHECK_FALSE(gone.value());
    }

    // list
    {
        auto flat = fs.list(root / eng::fs::Path{"scenes"}, false);
        REQUIRE(flat.ok());
        REQUIRE(flat.value().size() == 2);
        CHECK(flat.value()[0].path.filename().str() == "blob.bin");
        CHECK(flat.value()[1].path.filename().str() == "renamed.json");
    }

    // remove
    {
        auto r = fs.remove(root / eng::fs::Path{"empty.txt"});
        REQUIRE(r.ok());
        CHECK(r.value());
    }

    // inexistente → NotFound com código
    {
        auto r = fs.readAllText(root / eng::fs::Path{"nao-tem.txt"});
        REQUIRE(r.isError());
        CHECK(r.error().code == eng::core::StatusCode::NotFound);
        CHECK_FALSE(r.error().message.empty());
    }

    // path inválido → InvalidArgument
    {
        auto r = fs.writeAllText(eng::fs::Path{""}, "x");
        REQUIRE(r.isError());
        CHECK(r.error().code == eng::core::StatusCode::InvalidArgument);
    }

    tmp.cleanup();
}

TEST_CASE("fs: File RAII e move", "[fs][native]")
{
    TmpDir tmp;
    eng::fs::NativeFileSystem fs;
    const auto path = tmp.path() / eng::fs::Path{"f.txt"};

    {
        auto opened = eng::fs::File::open(path, eng::fs::FileOpenMode::Write);
        REQUIRE(opened.ok());
        CHECK(opened.value().isOpen());
        const std::vector<std::byte> data{0x01_b, 0x02_b, 0x03_b};
        auto put = opened.value().write(data);
        REQUIRE(put.ok());
        CHECK(put.value() == 3);
    } // fecha no destrutor

    {
        auto opened = eng::fs::File::open(path, eng::fs::FileOpenMode::Read);
        REQUIRE(opened.ok());
        auto size = opened.value().size();
        REQUIRE(size.ok());
        CHECK(size.value() == 3);

        std::vector<std::byte> out(3);
        auto got = opened.value().read(out);
        REQUIRE(got.ok());
        CHECK(got.value() == 3);
        CHECK(out[0] == 0x01_b);

        // move: handle único
        eng::fs::File moved = std::move(opened.value());
        CHECK(moved.isOpen());
        CHECK_FALSE(opened.value().isOpen());
    }

    // abrir ausente para leitura → NotFound
    {
        auto r = eng::fs::File::open(tmp.path() / eng::fs::Path{"ghost"},
                                     eng::fs::FileOpenMode::Read);
        REQUIRE(r.isError());
        CHECK(r.error().code == eng::core::StatusCode::NotFound);
    }
}

// =============================================================================
// RootedFileSystem — fronteira do workspace (RECOVERY P0)
//
// Regressão dos bugs do APK Android:
//   "AssetBrowser: destino absoluto é proibido"
//   "EditorDocument: caminho absoluto proibido"
// O root absoluto entra AQUI e morre AQUI; o editor só vê relativos.
// =============================================================================

TEST_CASE("fs: RootedFileSystem mapeia relativo → root absoluto", "[fs][native]")
{
    TmpDir tmp;
    eng::fs::NativeFileSystem native;
    // Simula o Android: workspace físico ABSOLUTO (filesDir/projects).
    eng::fs::RootedFileSystem fs{native, tmp.path()};

    REQUIRE(fs.mkdirs(eng::fs::Path{"MyGame/assets/textures"}));
    REQUIRE(fs.writeAllText(eng::fs::Path{"MyGame/assets/textures/g.png"},
                            "PNG"));

    // O mesmo conteúdo existe no físico (root/x) e não no CWD.
    {
        auto viaRoot = native.readAllText(
            tmp.path() / eng::fs::Path{"MyGame/assets/textures/g.png"});
        REQUIRE(viaRoot.ok());
        CHECK(viaRoot.value() == "PNG");
    }
    {
        auto leak = native.exists(eng::fs::Path{"MyGame/assets/textures/g.png"});
        REQUIRE(leak.ok());
        CHECK_FALSE(leak.value()); // CWD: não vaza para fora do root
    }

    // Formas com "./" e redundâncias: mesma localização (join normalizado).
    REQUIRE(fs.writeAllText(eng::fs::Path{"./MyGame/../MyGame/./a.txt"}, "A"));
    {
        auto text = fs.readAllText(eng::fs::Path{"MyGame/a.txt"});
        REQUIRE(text.ok());
        CHECK(text.value() == "A");
    }

    // exists(".") = o workspace inteiro.
    {
        auto all = fs.exists(eng::fs::Path{"."});
        REQUIRE(all.ok());
        CHECK(all.value());
    }
}

TEST_CASE("fs: RootedFileSystem rejeita absoluto e escape (fronteira)",
          "[fs][native]")
{
    TmpDir tmp;
    eng::fs::NativeFileSystem native;
    eng::fs::RootedFileSystem fs{native, tmp.path()};

    // O bug do APK: path ABSOLUTO cruzando a fronteira → InvalidArgument.
    // (Nunca foi para "aceitar absoluto" — a conversão é papel da fronteira.)
    const auto absolute = tmp.path() / eng::fs::Path{"MyGame/x.txt"};
    {
        auto r = fs.writeAllText(absolute, "x");
        REQUIRE(r.isError());
        CHECK(r.error().code == eng::core::StatusCode::InvalidArgument);
    }
    {
        auto r = fs.readAllBytes(absolute);
        REQUIRE(r.isError());
        CHECK(r.error().code == eng::core::StatusCode::InvalidArgument);
    }
    // Escape por ".." → InvalidArgument.
    {
        auto r = fs.mkdirs(eng::fs::Path{"../fora"});
        REQUIRE(r.isError());
        CHECK(r.error().code == eng::core::StatusCode::InvalidArgument);
    }
    {
        auto r = fs.writeAllText(eng::fs::Path{"a/../../escape"}, "x");
        REQUIRE(r.isError());
        CHECK(r.error().code == eng::core::StatusCode::InvalidArgument);
    }
    // Vazio/inválido → InvalidArgument (contrato do FileSystem).
    {
        auto r = fs.exists(eng::fs::Path{""});
        REQUIRE(r.isError());
        CHECK(r.error().code == eng::core::StatusCode::InvalidArgument);
    }
    // rename com destino absoluto → InvalidArgument (uma ponta basta p/ o
    // todo falhar — o move nunca acontece parcialmente).
    {
        auto r = fs.rename(eng::fs::Path{"MyGame/a.txt"}, absolute);
        REQUIRE(r.isError());
        CHECK(r.error().code == eng::core::StatusCode::InvalidArgument);
    }
}

TEST_CASE("fs: RootedFileSystem list devolve paths relativos (nunca o root)",
          "[fs][native]")
{
    TmpDir tmp;
    eng::fs::NativeFileSystem native;
    eng::fs::RootedFileSystem fs{native, tmp.path()};

    REQUIRE(fs.mkdirs(eng::fs::Path{"MyGame/assets/textures"}));
    REQUIRE(fs.mkdirs(eng::fs::Path{"MyGame/assets/scripts"}));
    REQUIRE(fs.writeAllText(eng::fs::Path{"MyGame/assets/textures/a.png"}, "1"));
    REQUIRE(fs.writeAllText(eng::fs::Path{"MyGame/assets/textures/b.png"}, "2"));
    REQUIRE(fs.writeAllText(eng::fs::Path{"MyGame/assets/scripts/m.nis"}, "3"));

    // Raso: paths root-relativos normalizados — a forma que o AssetBrowser
    // compara com meta.sourcePath.
    {
        auto listed = fs.list(eng::fs::Path{"MyGame/assets/textures"}, false);
        REQUIRE(listed.ok());
        REQUIRE(listed.value().size() == 2);
        CHECK(listed.value()[0].path.str() ==
              "MyGame/assets/textures/a.png");
        CHECK(listed.value()[1].path.str() ==
              "MyGame/assets/textures/b.png");
        CHECK_FALSE(listed.value()[0].isDirectory);
    }
    // Recursivo: hierarquia inteira, tudo relativo ao root.
    {
        auto listed = fs.list(eng::fs::Path{"MyGame"}, true);
        REQUIRE(listed.ok());
        REQUIRE(listed.value().size() == 6); // 3 dirs (assets, scripts,
        // textures) + 3 arquivos
        for (const auto& entry : listed.value()) {
            CHECK_FALSE(entry.path.isAbsolute());
            CHECK(entry.path.str().rfind(tmp.path().str(), 0) != 0);
        }
    }
    // Raiz do workspace: "." lista o topo.
    {
        auto listed = fs.list(eng::fs::Path{"."}, false);
        REQUIRE(listed.ok());
        REQUIRE(listed.value().size() == 1);
        CHECK(listed.value()[0].path.str() == "MyGame");
        CHECK(listed.value()[0].isDirectory);
    }
    // Ausente → NotFound (contrato preservado).
    {
        auto listed = fs.list(eng::fs::Path{"NaoExiste"}, false);
        REQUIRE(listed.isError());
        CHECK(listed.error().code == eng::core::StatusCode::NotFound);
    }
}

TEST_CASE("fs: RootedFileSystem sobre MemoryFileSystem (paridade de base)",
          "[fs]")
{
    // A base é qualquer FileSystem — a fronteira funciona igual sobre
    // Memory (testes) e Native (Android/Linux real).
    eng::fs::MemoryFileSystem mem;
    eng::fs::RootedFileSystem fs{mem, eng::fs::Path{"/data/user/0/app/files"}};

    REQUIRE(fs.mkdirs(eng::fs::Path{"projects/MyGame/assets"}));
    REQUIRE(fs.writeAllText(
        eng::fs::Path{"projects/MyGame/assets/asset_registry.json"}, "{}"));

    auto listed = fs.list(eng::fs::Path{"projects/MyGame/assets"}, false);
    REQUIRE(listed.ok());
    REQUIRE(listed.value().size() == 1);
    CHECK(listed.value()[0].path.str() ==
          "projects/MyGame/assets/asset_registry.json");

    auto text = fs.readAllText(
        eng::fs::Path{"projects/MyGame/assets/asset_registry.json"});
    REQUIRE(text.ok());
    CHECK(text.value() == "{}");
}
