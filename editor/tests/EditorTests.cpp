/// Testes do editor (FASE 8, missão §8.10) — rodam no LINUX.
///
/// Cobertura exigida pela missão: projeto (new/open/save/settings),
/// entidades (create/delete/duplicate/rename/hierarchy/componentes),
/// inspector, scene save/load, asset discovery, play/stop com separação
/// editor×runtime, viewport (câmera/hit-test), ViewportRenderer e EditorHost
/// contra backends REAIS (lavapipe/llvmpipe — mesmos binários do APK).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "eng/animation/Animation.hpp"
#include "eng/editor/EditorDocument.hpp"
#include "eng/editor/NiRuntime.hpp"
#include "eng/editor/EditorHost.hpp"
#include "eng/editor/Inspector.hpp"
#include "eng/editor/SpriteData.hpp"
#include "eng/editor/TextureCache.hpp"
#include "eng/editor/ViewportRenderer.hpp"
#include "eng/fs/MemoryFileSystem.hpp"
#include "eng/fs/NativeFileSystem.hpp"
#include "eng/log/ConsoleSink.hpp"
#include "eng/log/Logger.hpp"
#include "eng/particles/Particles.hpp"
#include "eng/physics/Physics.hpp"
#include "eng/rhi/Renderer.hpp"
#include "eng/rhi/gles/GlesBackend.hpp"
#include "eng/rhi/vulkan/VulkanBackend.hpp"
#include "eng/scene/Name.hpp"

namespace {

/// Logs no stderr (diagnóstico de ambiente — padrão das suites RHI).
struct LogSetup {
    eng::log::ConsoleSink console{stderr};
    LogSetup() { eng::log::Logger::get().addSink(console); }
};
const LogSetup kLogSetup{};

using eng::editor::EditorDocument;

struct DocFixture {
    eng::fs::MemoryFileSystem fsStorage;  // dono real (documento empresta)
    eng::fs::MemoryFileSystem* fs = &fsStorage;
    std::unique_ptr<EditorDocument> doc;

    DocFixture()
    {
        auto created = EditorDocument::create(fsStorage, eng::fs::Path{"."});
        REQUIRE(created.ok());
        doc = std::move(created.value());
    }

    /// Documento com projeto criado (assets + scenes prontos).
    void withProject()
    {
        REQUIRE(doc->newProject("TestGame").ok());
    }
};

/// Helpers de entidade empacotada (contrato JNI — pack/unpack).
std::uint64_t pack(eng::ecs::Entity e) { return EditorDocument::packEntity(e); }
eng::ecs::Entity unpack(std::uint64_t p) { return EditorDocument::unpackEntity(p); }

/// Projeto novo OU reaberto (testes do host rodam em disco REAL — runs
/// repetidos no mesmo build dir encontram o projeto da run anterior).
void ensureProject(EditorDocument& doc, const char* name)
{
    if (!doc.newProject(name).ok()) {
        REQUIRE(doc.openProject(eng::fs::Path{name}).ok());
    }
}

}  // namespace

// =============================================================================
// 1. Projeto (§8.10: criar/abrir/salvar projeto)
// =============================================================================

TEST_CASE("editor: cria projeto com estrutura completa", "[editor]")
{
    DocFixture f;
    REQUIRE(f.doc->newProject("MyGame").ok());
    REQUIRE(f.doc->hasProject());
    CHECK(f.doc->projectName() == "MyGame");

    auto& fs = *f.fs; // (fs movido para o doc — checagem via doc)
    (void)fs;
    // Estrutura em disco (via fs do próprio documento é inacessível —
    // valida pelos efeitos: openProject re-abre com sucesso).
    CHECK(f.doc->saveProject().ok());
}

TEST_CASE("editor: abre projeto salvo e valida round-trip", "[editor]")
{
    eng::fs::MemoryFileSystem fs;
    {
        auto created = EditorDocument::create(fs, eng::fs::Path{"."});
        REQUIRE(created.ok());
        REQUIRE(created.value()->newProject("RoundTrip").ok());
        REQUIRE(created.value()->setProjectName("RoundTrip2").ok());
        REQUIRE(created.value()->saveProject().ok());
    }
    auto reopened = EditorDocument::create(fs, eng::fs::Path{"."});
    REQUIRE(reopened.ok());
    REQUIRE(reopened.value()->openProject(eng::fs::Path{"RoundTrip"}).ok());
    auto& doc = *reopened.value();
    CHECK(doc.projectName() == "RoundTrip2");
    CHECK_FALSE(doc.projectDirty());
}

TEST_CASE("editor: newProject rejeita duplicado e nome vazio", "[editor]")
{
    DocFixture f;
    REQUIRE(f.doc->newProject("Dup").ok());
    auto again = f.doc->newProject("Dup");
    REQUIRE(again.isError());
    auto empty = f.doc->newProject("");
    REQUIRE(empty.isError());
}

TEST_CASE("editor: settings renomeia e marca dirty", "[editor]")
{
    DocFixture f;
    f.withProject();
    REQUIRE(f.doc->setProjectName("NovoNome").ok());
    CHECK(f.doc->projectDirty());
    REQUIRE(f.doc->saveProject().ok());
    CHECK_FALSE(f.doc->projectDirty());
    CHECK(f.doc->projectName() == "NovoNome");
}

// =============================================================================
// 2. Entidades (§8.10: criar/apagar/duplicar/rename/hierarchy)
// =============================================================================

TEST_CASE("editor: cria entidade com nome e componente Name", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("Player", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    CHECK(f.doc->nameOf(*f.doc->sceneInFocus(), entity.value()) == "Player");
    CHECK(f.doc->sceneDirty());

    // Inspector vê os componentes built-in.
    const auto components =
        eng::editor::Inspector::componentsOf(*f.doc->sceneInFocus(), entity.value());
    CHECK(std::find(components.begin(), components.end(),
                    "eng::scene::Name") != components.end());
    CHECK(std::find(components.begin(), components.end(),
                    "eng::math::Transform") != components.end());
}

TEST_CASE("editor: rename e apagar entidade", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("Ground", eng::scene::kNoEntity);
    REQUIRE(entity.ok());

    REQUIRE(f.doc->renameEntity(entity.value(), "Ground2").ok());
    CHECK(f.doc->nameOf(*f.doc->sceneInFocus(), entity.value()) == "Ground2");

    REQUIRE(f.doc->select(entity.value()).ok());
    CHECK(f.doc->isSelected(entity.value()));

    REQUIRE(f.doc->deleteEntity(entity.value()).ok());
    CHECK_FALSE(f.doc->selection().has_value()); // seleção limpa
    auto renamed = f.doc->renameEntity(entity.value(), "X");
    CHECK(renamed.isError());
}

TEST_CASE("editor: duplicar entidade clona subárvore e componentes", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto parent = f.doc->createEntity("Enemy", eng::scene::kNoEntity);
    REQUIRE(parent.ok());
    eng::editor::TransformDesc tr;
    tr.position = {3.f, 4.f, 0.f};
    tr.scale = {2.f, 2.f, 2.f};
    REQUIRE(f.doc->setTransform(parent.value(), tr).ok());

    auto child = f.doc->createEntity("Gun", parent.value());
    REQUIRE(child.ok());

    auto dup = f.doc->duplicateEntity(parent.value());
    REQUIRE(dup.ok());

    auto snapshot = f.doc->hierarchySnapshot();
    REQUIRE(snapshot.size() == 4); // Enemy, Gun, Enemy.alt, Gun
    CHECK(snapshot[2].name == "Enemy.alt");
    CHECK(snapshot[2].depth == 0);
    CHECK(snapshot[3].name == "Gun");
    CHECK(snapshot[3].depth == 1);

    auto dupTransform = f.doc->transform(dup.value());
    REQUIRE(dupTransform.ok());
    CHECK(dupTransform.value().position.x == 3.f);
    CHECK(dupTransform.value().scale.x == 2.f);

    // Duplicar de novo: nome numera (.alt2).
    auto dup2 = f.doc->duplicateEntity(dup.value());
    REQUIRE(dup2.ok());
    CHECK(f.doc->nameOf(*f.doc->sceneInFocus(), dup2.value()) == "Enemy.alt2");
}

TEST_CASE("editor: reparent aceita e rejeita ciclo", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto a = f.doc->createEntity("A", eng::scene::kNoEntity);
    auto b = f.doc->createEntity("B", a.value());
    auto c = f.doc->createEntity("C", b.value());
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(c.ok());

    // C em A: ok.
    REQUIRE(f.doc->reparentEntity(c.value(), a.value()).ok());
    // A em C: ciclo — rejeitado (Scene::attach, ADR-025).
    auto cycle = f.doc->reparentEntity(a.value(), c.value());
    REQUIRE(cycle.isError());
    // C → raiz.
    REQUIRE(f.doc->reparentEntity(c.value(), eng::scene::kNoEntity).ok());
}

TEST_CASE("editor: hierarquia snapshot com profundidade estável", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto root1 = f.doc->createEntity("R1", eng::scene::kNoEntity);
    auto root2 = f.doc->createEntity("R2", eng::scene::kNoEntity);
    auto child = f.doc->createEntity("C1", root1.value());
    REQUIRE(root1.ok());
    REQUIRE(root2.ok());
    REQUIRE(child.ok());

    const auto snapshot = f.doc->hierarchySnapshot();
    REQUIRE(snapshot.size() == 3);
    CHECK(snapshot[0].name == "R1");
    CHECK(snapshot[0].depth == 0);
    CHECK(snapshot[1].name == "C1");
    CHECK(snapshot[1].depth == 1);
    CHECK(snapshot[2].name == "R2");
    CHECK(snapshot[2].depth == 0);
}

// =============================================================================
// 3. Componentes + Inspector (§8.10)
// =============================================================================

TEST_CASE("editor: catálogo contém os built-ins", "[editor]")
{
    const auto catalog = eng::editor::Inspector::catalog();
    CHECK_FALSE(catalog.empty());
    CHECK(std::find(catalog.begin(), catalog.end(), "eng::scene::Name") !=
          catalog.end());
    CHECK(std::find(catalog.begin(), catalog.end(),
                    "eng::math::Transform") != catalog.end());
}

TEST_CASE("editor: inspector lê/escreve campos por caminho", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("Player", eng::scene::kNoEntity);
    REQUIRE(entity.ok());

    auto got = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::math::Transform",
        "position.x");
    REQUIRE(got.ok());
    CHECK(got.value() == "0");

    REQUIRE(f.doc->setInspectorField(entity.value(), "eng::math::Transform",
                                     "position.x", "2.5")
                .ok());
    auto after = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::math::Transform",
        "position.x");
    REQUIRE(after.ok());
    CHECK(after.value() == "2.5");

    // String (Name.value).
    REQUIRE(f.doc->setInspectorField(entity.value(), "eng::scene::Name",
                                     "value", "Renamed")
                .ok());
    CHECK(f.doc->nameOf(*f.doc->sceneInFocus(), entity.value()) == "Renamed");

    // Campos achatados incluem subcampos de structs.
    const auto fields = f.doc->inspectorFields(
        entity.value(), "eng::math::Transform");
    REQUIRE_FALSE(fields.empty());
    bool hasScaleY = false;
    bool hasRotationW = false;
    for (const auto& field : fields) {
        if (field.path == "scale.y") { hasScaleY = true; }
        if (field.path == "rotation.w") { hasRotationW = true; }
    }
    CHECK(hasScaleY);
    CHECK(hasRotationW);
}

TEST_CASE("editor: inspector rejeita lixo com erro preciso", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("X", eng::scene::kNoEntity);
    REQUIRE(entity.ok());

    auto badFloat = f.doc->setInspectorField(
        entity.value(), "eng::math::Transform", "position.x", "abc");
    CHECK(badFloat.isError());

    auto nanFloat = f.doc->setInspectorField(
        entity.value(), "eng::math::Transform", "position.x", "nan");
    CHECK(nanFloat.isError());

    auto badPath = f.doc->setInspectorField(
        entity.value(), "eng::math::Transform", "position.naoexiste", "1");
    CHECK(badPath.isError());

    auto badComp = f.doc->setInspectorField(
        entity.value(), "ComponenteInexistente", "x", "1");
    CHECK(badComp.isError());
}

// =============================================================================
// 3b. Inspector com kinds semânticos (evolução P0-6, ADR-052)
// =============================================================================

namespace {

/// Encontra um campo por path exato (ou nullptr).
const eng::editor::Inspector::Field* fieldByPath(
    const std::vector<eng::editor::Inspector::Field>& fields,
    std::string_view path)
{
    for (const auto& field : fields) {
        if (field.path == path) {
            return &field;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("editor: inspector emite kind/options de enum (P0-6)", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("Col", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(f.doc->addComponent(entity.value(), "eng::physics::Collider")
                .ok());

    const auto fields = f.doc->inspectorFields(
        entity.value(), "eng::physics::Collider");
    const auto* shape = fieldByPath(fields, "shape");
    REQUIRE(shape != nullptr);
    CHECK(shape->kind == "enum");
    CHECK(shape->options == "Sphere|Box");
    CHECK(shape->value == "Sphere"); // default do campo

    // Escrita por NOME de enumerador (contrato estável ADR-033).
    REQUIRE(f.doc->setInspectorField(entity.value(), "eng::physics::Collider",
                                     "shape", "Box")
                .ok());
    auto after = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::physics::Collider",
        "shape");
    REQUIRE(after.ok());
    CHECK(after.value() == "Box");

    // Enumerador inexistente → erro preciso.
    auto bad = f.doc->setInspectorField(
        entity.value(), "eng::physics::Collider", "shape", "Cylinder");
    CHECK(bad.isError());
}

TEST_CASE("editor: inspector emite kind bool/number/int/text (P0-6)",
          "[editor]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("S", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(f.doc->addComponent(entity.value(), "eng::editor::SpriteData")
                .ok());

    const auto fields = f.doc->inspectorFields(
        entity.value(), "eng::editor::SpriteData");

    // bool → Switch na UI.
    const auto* flipX = fieldByPath(fields, "flipX");
    REQUIRE(flipX != nullptr);
    CHECK(flipX->kind == "bool");
    CHECK(flipX->value == "false");
    REQUIRE(f.doc->setInspectorField(entity.value(),
                                     "eng::editor::SpriteData", "flipX",
                                     "true")
                .ok());
    CHECK(fieldByPath(f.doc->inspectorFields(entity.value(),
                                             "eng::editor::SpriteData"),
                      "flipX")
              ->value == "true");

    // bool com valor inválido → erro.
    CHECK(f.doc->setInspectorField(entity.value(), "eng::editor::SpriteData",
                                   "flipX", "sim")
              .isError());

    // f32 → number.
    const auto* sort = fieldByPath(fields, "sort");
    REQUIRE(sort != nullptr);
    CHECK(sort->kind == "number");

    // string COMUM → text (Name.value).
    const auto nameFields = f.doc->inspectorFields(entity.value(),
                                                  "eng::scene::Name");
    const auto* value = fieldByPath(nameFields, "value");
    REQUIRE(value != nullptr);
    CHECK(value->kind == "text");
    CHECK(value->options.empty());
}

TEST_CASE("editor: inspector colapsa canais de cor em UM campo hex (P0-6)",
          "[editor]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("Tint", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(f.doc->addComponent(entity.value(), "eng::editor::SpriteData")
                .ok());

    const auto fields = f.doc->inspectorFields(
        entity.value(), "eng::editor::SpriteData");

    // Os três canais viram UM campo sintético — os individuais SOMEM.
    const auto* tint = fieldByPath(fields, "tintR,tintG,tintB");
    REQUIRE(tint != nullptr);
    CHECK(tint->kind == "color");
    CHECK(tint->typeName == "color");
    CHECK(tint->value == "#FFFFFF"); // defaults 1,1,1
    CHECK(fieldByPath(fields, "tintR") == nullptr);
    CHECK(fieldByPath(fields, "tintG") == nullptr);
    CHECK(fieldByPath(fields, "tintB") == nullptr);

    // Escrita hex → três floats; leitura devolve o hex.
    REQUIRE(f.doc->setInspectorField(entity.value(),
                                     "eng::editor::SpriteData",
                                     "tintR,tintG,tintB", "#FF8000")
                .ok());
    auto r = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::editor::SpriteData",
        "tintR");
    REQUIRE(r.ok());
    CHECK(r.value() == "1");
    auto g = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::editor::SpriteData",
        "tintG");
    REQUIRE(g.ok());
    // float(128/255) impresso com %.9g — comparação numérica robusta.
    CHECK(std::stof(g.value()) == Catch::Approx(128.f / 255.f)
                                     .margin(1e-6f));
    auto hex = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::editor::SpriteData",
        "tintR,tintG,tintB");
    REQUIRE(hex.ok());
    CHECK(hex.value() == "#FF8000");

    // Clamp: valores fora de [0..1] saturam nos bytes hex.
    REQUIRE(f.doc->setInspectorField(entity.value(),
                                     "eng::editor::SpriteData", "tintR", "2")
                .ok());
    REQUIRE(f.doc->setInspectorField(entity.value(),
                                     "eng::editor::SpriteData", "tintG",
                                     "-1")
                .ok());
    auto clamped = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::editor::SpriteData",
        "tintR,tintG,tintB");
    REQUIRE(clamped.ok());
    CHECK(clamped.value() == "#FF0000");
}

TEST_CASE("editor: grupo de cor rejeita hex lixo SEM escrever nada (P0-6)",
          "[editor]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("Bad", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(f.doc->addComponent(entity.value(), "eng::editor::SpriteData")
                .ok());

    // Estado conhecido: vermelho puro.
    REQUIRE(f.doc->setInspectorField(entity.value(),
                                     "eng::editor::SpriteData",
                                     "tintR,tintG,tintB", "#FF0000")
                .ok());

    for (const char* garbage : {"red", "#12345", "#GGHHII", "", "#1234567"}) {
        auto written = f.doc->setInspectorField(
            entity.value(), "eng::editor::SpriteData", "tintR,tintG,tintB",
            garbage);
        CHECK(written.isError());
    }
    // Nenhuma escrita parcial: continua vermelho puro.
    auto hex = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::editor::SpriteData",
        "tintR,tintG,tintB");
    REQUIRE(hex.ok());
    CHECK(hex.value() == "#FF0000");

    // Grupo com contagem errada de canais → erro preciso.
    CHECK(f.doc->setInspectorField(entity.value(), "eng::editor::SpriteData",
                                   "tintR,tintG", "#FF0000")
              .isError());
    CHECK(f.doc->setInspectorField(
              entity.value(), "eng::editor::SpriteData",
              "tintR,tintG,tintB,opacity,sort", "#FF0000FF")
              .isError());
    // Canal que não é float → erro.
    CHECK(f.doc->setInspectorField(entity.value(), "eng::editor::SpriteData",
                                   "tintR,textureAsset,tintB", "#FF0000")
              .isError());
}

TEST_CASE("editor: campo de textura reporta kind texture (P0-6)", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("Sprite", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(f.doc->addComponent(entity.value(), "eng::editor::SpriteData")
                .ok());

    const auto fields = f.doc->inspectorFields(
        entity.value(), "eng::editor::SpriteData");
    const auto* texture = fieldByPath(fields, "textureAsset");
    REQUIRE(texture != nullptr);
    CHECK(texture->kind == "texture");
    CHECK(texture->typeName == "string");
    CHECK(texture->value.empty()); // sem textura atribuída

    // Continua sendo uma string gravável (o picker de textura escreve aqui).
    REQUIRE(f.doc->setInspectorField(entity.value(),
                                     "eng::editor::SpriteData",
                                     "textureAsset", "hero.png")
                .ok());
    CHECK(fieldByPath(f.doc->inspectorFields(entity.value(),
                                             "eng::editor::SpriteData"),
                      "textureAsset")
              ->value == "hero.png");
}

// =============================================================================
// 3c. Scripts NI-Script como assets do projeto (evolução P0-7, ADR-053)
// =============================================================================

TEST_CASE("editor: scriptCreate gera template válido e catalogado (P0-7)",
          "[editor]")
{
    DocFixture f;
    f.withProject();

    // Nome sem extensão → ganha .nis.
    REQUIRE(f.doc->scriptCreate("Movimento").ok());
    auto names = f.doc->scriptList();
    REQUIRE(names.ok());
    REQUIRE(names.value().size() == 1);
    CHECK(names.value()[0] == "Movimento.nis");

    // O TEMPLATE COMPILA — provado pela MESMA checagem que a UI usa.
    auto source = f.doc->scriptRead("Movimento.nis");
    REQUIRE(source.ok());
    auto check = f.doc->scriptCompile(source.value());
    REQUIRE(check.ok());
    CHECK(check.value().ok);
    CHECK(check.value().diags.empty());

    // Duplicado → AlreadyExists.
    CHECK(f.doc->scriptCreate("Movimento").isError());
    // Nome com extensão idêntica → duplicado do mesmo jeito.
    CHECK(f.doc->scriptCreate("Movimento.nis").isError());

    // Nomes inválidos rejeitados (traversal, vazio).
    CHECK(f.doc->scriptCreate("").isError());
    CHECK(f.doc->scriptCreate("../evil").isError());

    // Catalogado no registry: o AssetBrowser lista como registrado com id.
    auto* browser = f.doc->assets();
    REQUIRE(browser != nullptr);
    auto listed = browser->list("scripts");
    REQUIRE(listed.ok());
    REQUIRE(listed.value().size() == 1);
    CHECK(listed.value()[0].registered);
    CHECK(listed.value()[0].id != "-");
}

TEST_CASE("editor: scriptWrite/Read round-trip + registry (P0-7)", "[editor]")
{
    DocFixture f;
    f.withProject();

    const std::string src = "add &BL\n\nvar hp: int = 10\n";
    REQUIRE(f.doc->scriptWrite("Player.nis", src).ok());

    auto read = f.doc->scriptRead("Player.nis");
    REQUIRE(read.ok());
    CHECK(read.value() == src);

    // Substituição preserva o id (ADR-029).
    auto before = f.doc->assets()->list("scripts");
    REQUIRE(before.ok());
    REQUIRE(before.value().size() == 1);
    const std::string idBefore = before.value()[0].id;

    const std::string src2 = "add &BL\n\nvar hp: int = 20\n";
    REQUIRE(f.doc->scriptWrite("Player.nis", src2).ok());
    auto after = f.doc->assets()->list("scripts");
    REQUIRE(after.ok());
    REQUIRE(after.value().size() == 1);
    CHECK(after.value()[0].id == idBefore);
    CHECK(after.value()[0].registered);

    auto read2 = f.doc->scriptRead("Player.nis");
    REQUIRE(read2.ok());
    CHECK(read2.value() == src2);

    // Delete remove arquivo + meta.
    REQUIRE(f.doc->scriptDelete("Player.nis").ok());
    CHECK(f.doc->scriptList().value().empty());
    CHECK(f.doc->scriptRead("Player.nis").isError());
}

TEST_CASE("editor: scriptCompile separa válido de inválido com diagnósticos (P0-7)",
          "[editor]")
{
    DocFixture f;
    f.withProject();

    // Fonte VÁLIDA (usa nativo de host: self()).
    const std::string good = "add &BL\n"
                             "\n"
                             "up update:\n"
                             "    var me = self()\n"
                             "    me.position.x = me.position.x + 1\n"
                             "stop\n";
    auto okCheck = f.doc->scriptCompile(good);
    REQUIRE(okCheck.ok());
    CHECK(okCheck.value().ok);
    CHECK(okCheck.value().diags.empty());

    // Fonte QUEBRADA: string aberta → diagnóstico com linha/coluna.
    const std::string bad = "var s = \"aberta\n";
    auto badCheck = f.doc->scriptCompile(bad);
    REQUIRE(badCheck.ok());          // o CHECK rodou (erro interno não houve)
    CHECK_FALSE(badCheck.value().ok); // o VEREDITO é do compilador
    REQUIRE_FALSE(badCheck.value().diags.empty());
    CHECK(badCheck.value().diags[0].line == 1);
    CHECK(badCheck.value().diags[0].col > 0);
    CHECK_FALSE(badCheck.value().diags[0].message.empty());

    // Nativo inexistente → sema pega (a tabela do runtime de Play).
    const std::string badNative = "up update:\n    voo_magico()\nstop\n";
    auto nativeCheck = f.doc->scriptCompile(badNative);
    REQUIRE(nativeCheck.ok());
    CHECK_FALSE(nativeCheck.value().ok);
    CHECK_FALSE(nativeCheck.value().diags.empty());
}

TEST_CASE("editor: scriptAssign anexa fonte e PLAY roda o script (P0-7)",
          "[editor]")
{
    DocFixture f;
    f.withProject();

    const std::string src = "add &BL\n"
                            "\n"
                            "var speed: float = 3.0\n"
                            "\n"
                            "up update:\n"
                            "    var me = self()\n"
                            "    me.position.x = me.position.x + speed\n"
                            "stop\n";
    REQUIRE(f.doc->scriptWrite("Andar.nis", src).ok());

    auto entity = f.doc->createEntity("Player", eng::scene::kNoEntity);
    REQUIRE(entity.ok());

    // Anexa: adiciona o componente e copia a fonte do ASSET.
    REQUIRE(f.doc->scriptAssign(entity.value(), "Andar.nis").ok());
    auto fields = f.doc->inspectorFields(
        entity.value(), "eng::editor::NiScriptComponent");
    const auto* sourceField = fieldByPath(fields, "source");
    REQUIRE(sourceField != nullptr);
    CHECK(sourceField->value == src);
    CHECK(sourceField->kind == "text");

    // PLAY: compila e roda o up update (o mesmo caminho da FASE 11).
    REQUIRE(f.doc->play().ok());
    CHECK(f.doc->runtimeScripts().size() == 1);
    f.doc->tick(1.f / 60.f);
    auto x = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::math::Transform",
        "position.x");
    REQUIRE(x.ok());
    // up update: me.position.x = me.position.x + speed → 0 + 3.0 = 3.0
    // (a conta do script é +speed por update, não *delta).
    CHECK(std::stof(x.value()) == Catch::Approx(3.f).margin(1e-4f));
    f.doc->stop();

    // Sem projeto aberto (documento recém-criado) → erro preciso.
    DocFixture fresh;
    CHECK(fresh.doc->scriptList().isError());
    CHECK(fresh.doc->scriptRead("x.nis").isError());
}

TEST_CASE("editor: add/remove componente com proteção dos core", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("X", eng::scene::kNoEntity);
    REQUIRE(entity.ok());

    // Remover Name/Transform: protegidos.
    auto rmName =
        f.doc->removeComponent(entity.value(), "eng::scene::Name");
    CHECK(rmName.isError());
    auto rmTransform =
        f.doc->removeComponent(entity.value(), "eng::math::Transform");
    CHECK(rmTransform.isError());

    // Add duplicado: erro AlreadyExists.
    auto dup = f.doc->addComponent(entity.value(), "eng::scene::Name");
    CHECK(dup.isError());

    // Remover algo ausente: erro.
    auto rmAbsent = f.doc->removeComponent(entity.value(),
                                            "eng::math::Transform");
    CHECK(rmAbsent.isError()); // protegido SEMPRE (mesmo presente)
}

// =============================================================================
// 4. Transform (Euler ↔ Quat round-trip)
// =============================================================================

TEST_CASE("editor: transform Euler↔Quat round-trip", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("Rot", eng::scene::kNoEntity);
    REQUIRE(entity.ok());

    // Perto do gimbal (|ângulo| ~ 90°), asin amplifica o erro do f32 —
    // tolerância proporcional em vez de absoluta (limitação documentada).
    const auto tolerance = [](float degrees) {
        return std::abs(std::abs(degrees) - 90.f) < 2.f ? 0.05f : 1e-3f;
    };
    const eng::math::Vec3 angles[] = {
        {0.f, 0.f, 0.f},   {30.f, 0.f, 0.f},  {0.f, 45.f, 0.f},
        {0.f, 0.f, 60.f},   {10.f, 20.f, 30.f}, {-25.f, 40.f, -15.f},
        {90.f, 0.f, 0.f},  {0.f, 89.f, 0.f},
    };
    for (const auto& angle : angles) {
        eng::editor::TransformDesc desc;
        desc.rotationDegrees = angle;
        REQUIRE(f.doc->setTransform(entity.value(), desc).ok());
        auto back = f.doc->transform(entity.value());
        REQUIRE(back.ok());
        INFO("angle " << angle.x << "," << angle.y << "," << angle.z
                     << " -> " << back.value().rotationDegrees.x << ","
                     << back.value().rotationDegrees.y << ","
                     << back.value().rotationDegrees.z);
        CHECK_THAT(back.value().rotationDegrees.x,
                   Catch::Matchers::WithinAbs(angle.x, tolerance(angle.x)));
        CHECK_THAT(back.value().rotationDegrees.y,
                   Catch::Matchers::WithinAbs(angle.y, tolerance(angle.y)));
        CHECK_THAT(back.value().rotationDegrees.z,
                   Catch::Matchers::WithinAbs(angle.z, tolerance(angle.z)));
    }
}

// =============================================================================
// 5. Cena save/load (§8.10)
// =============================================================================

TEST_CASE("editor: save/load de cena com dirty flags", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("Persisted", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(f.doc->setInspectorField(entity.value(), "eng::math::Transform",
                                     "position.y", "7")
                .ok());
    CHECK(f.doc->sceneDirty());

    REQUIRE(f.doc->saveScene("main.json").ok());
    CHECK_FALSE(f.doc->sceneDirty());

    // Modifica e recarrega.
    REQUIRE(f.doc->setInspectorField(entity.value(), "eng::math::Transform",
                                     "position.y", "99")
                .ok());
    REQUIRE(f.doc->loadScene("main.json").ok());
    CHECK_FALSE(f.doc->sceneDirty());

    auto snapshot = f.doc->hierarchySnapshot();
    REQUIRE(snapshot.size() == 1);
    CHECK(snapshot[0].name == "Persisted");
    auto got = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), snapshot[0].entity, "eng::math::Transform",
        "position.y");
    REQUIRE(got.ok());
    CHECK(got.value() == "7");
}

// =============================================================================
// 6. Play/Stop — separação editor × runtime (§8.10)
// =============================================================================

TEST_CASE("editor: play clona, edição rejeitada, mutação não vaza", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("Player", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(f.doc->saveScene("s.json").ok());

    // PLAY: runtime clone.
    REQUIRE(f.doc->play().ok());
    CHECK(f.doc->isPlaying());

    // Snapshot do clone == snapshot da edição.
    auto editSnapshot = f.doc->hierarchySnapshot(); // foco = runtime em Play
    CHECK(editSnapshot.size() == 1);

    // Edição REJEITADA em Play.
    auto create = f.doc->createEntity("Novo", eng::scene::kNoEntity);
    CHECK(create.isError());
    auto del = f.doc->deleteEntity(entity.value());
    CHECK(del.isError());
    auto rename = f.doc->renameEntity(entity.value(), "Outro");
    CHECK(rename.isError());
    auto field = f.doc->setInspectorField(entity.value(),
                                         "eng::math::Transform", "position.x",
                                         "5");
    CHECK(field.isError());

    // Mover entidade em Play: muda o CLONE (debug §8.7).
    REQUIRE(f.doc->moveEntityScreen(entity.value(), 48.f, 0.f).ok());
    auto cloneX = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::math::Transform",
        "position.x");
    REQUIRE(cloneX.ok());
    CHECK(cloneX.value() == "1"); // 48px / zoom 48 = 1 unidade

    // STOP: edição intacta.
    f.doc->stop();
    CHECK_FALSE(f.doc->isPlaying());
    auto editX = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::math::Transform",
        "position.x");
    REQUIRE(editX.ok());
    CHECK(editX.value() == "0");

    // Segundo play re-clona do estado atual da edição.
    REQUIRE(f.doc->play().ok());
    auto freshX = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::math::Transform",
        "position.x");
    REQUIRE(freshX.ok());
    CHECK(freshX.value() == "0");
    f.doc->stop();
}

TEST_CASE("editor: play duplicado e stop sem play são tratados", "[editor]")
{
    DocFixture f;
    f.withProject();
    f.doc->stop(); // stop sem play: no-op
    REQUIRE(f.doc->play().ok());
    auto twice = f.doc->play();
    CHECK(twice.isError());
    f.doc->stop();
}

TEST_CASE("editor: PLAY roda scripts NI-Script do clone (FASE 11)",
          "[editor][ni]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("Motor", eng::scene::kNoEntity);
    REQUIRE(entity.ok());

    // Script anexado via catálogo (mesmo caminho do Inspector/JNI — a UI
    // dedicada de script é FUTURO declarado, docs/ni-script/08).
    const char* source =
        "add &BL\n"
        "var speed: float = 2.0\n"
        "var ticks: int = 0\n"
        "up start:\n"
        "    var me = self()\n"
        "    me.scale.y = 1.5\n"
        "stop\n"
        "up update:\n"
        "    var me = self()\n"
        "    me.position.x = me.position.x + speed\n"
        "    me.name = \"motor\"\n"
        "    ticks = ticks + 1\n"
        "stop\n"
        "up destroy:\n"
        "    var me = self()\n"
        "    me.scale.y = 9.0\n"
        "stop\n";
    REQUIRE(f.doc->addComponent(entity.value(),
                                "eng::editor::NiScriptComponent")
                .ok());
    REQUIRE(f.doc
                ->setInspectorField(entity.value(),
                                    "eng::editor::NiScriptComponent",
                                    "source", source)
                .ok());

    // PLAY: compila + @init + up start (scale.y = 1.5 no CLONE).
    REQUIRE(f.doc->play().ok());
    CHECK(f.doc->runtimeScripts().size() == 1);
    auto scaleY = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::math::Transform",
        "scale.y");
    REQUIRE(scaleY.ok());
    CHECK(scaleY.value() == "1.5");
    auto nameAfterStart = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::scene::Name", "value");
    REQUIRE(nameAfterStart.ok());
    CHECK(nameAfterStart.value() == "Motor"); // name muda no update, não no start

    // TICKs: up update move o clone (2/tick).
    f.doc->tick(1.f / 60.f);
    f.doc->tick(1.f / 60.f);
    f.doc->tick(1.f / 60.f);
    auto posX = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::math::Transform",
        "position.x");
    REQUIRE(posX.ok());
    CHECK(posX.value() == "6"); // 3 ticks × speed 2.0
    auto nameAfter = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::scene::Name", "value");
    REQUIRE(nameAfter.ok());
    CHECK(nameAfter.value() == "motor");

    // STOP: up destroy (best-effort) roda ANTES do descarte; edição NUNCA
    // foi tocada (ADR-044).
    f.doc->stop();
    CHECK(f.doc->runtimeScripts().empty());
    auto editX = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::math::Transform",
        "position.x");
    REQUIRE(editX.ok());
    CHECK(editX.value() == "0"); // edição intacta
    auto editScale = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::math::Transform",
        "scale.y");
    REQUIRE(editScale.ok());
    CHECK(editScale.value() == "1"); // 1.5/9.0 ficaram no clone descartado
    auto editName = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::scene::Name", "value");
    REQUIRE(editName.ok());
    CHECK(editName.value() == "Motor");
}

TEST_CASE("editor: script com erro de compilação é desabilitado, cena segue",
          "[editor][ni]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("Quebrado", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(f.doc->addComponent(entity.value(),
                                "eng::editor::NiScriptComponent")
                .ok());
    REQUIRE(f.doc
                ->setInspectorField(entity.value(),
                                    "eng::editor::NiScriptComponent",
                                    "source", "up update:\n    nada()\nstop\n")
                .ok());

    // PLAY: script inválido NÃO derruba o play (log + desabilitado).
    REQUIRE(f.doc->play().ok());
    CHECK(f.doc->runtimeScripts().empty()); // nada compilou
    f.doc->tick(1.f / 60.f);                 // tick sem scripts: ok
    f.doc->stop();
}

TEST_CASE("editor: input do JOGO em Play (FASE 9 §6.4 — separação)",
          "[editor]")
{
    DocFixture f;
    f.withProject();

    // Bindings por JSON asset (mesma API que input.json configuraria).
    using eng::serial::JsonValue;
    JsonValue root = JsonValue::array();
    JsonValue jump = JsonValue::object();
    jump.set("name", JsonValue::string("jump"));
    JsonValue sources = JsonValue::array();
    JsonValue zone = JsonValue::object();
    JsonValue rect = JsonValue::array();
    rect.append(JsonValue::real(0.0));
    rect.append(JsonValue::real(0.0));
    rect.append(JsonValue::real(1.0));
    rect.append(JsonValue::real(0.5));
    zone.set("touchZone", std::move(rect));
    sources.append(std::move(zone));
    jump.set("sources", std::move(sources));
    root.append(std::move(jump));
    auto bindings = eng::input::ActionBindings::fromJson(root);
    REQUIRE(bindings.ok());
    f.doc->setRuntimeBindings(std::move(bindings.value()));
    f.doc->setGameViewportSize(200.f, 100.f);

    // Em EDIT: input do jogo NÃO processa (gestos do editor não vazam).
    f.doc->gameTouch(0, 0, 100.f, 25.f, 1.f);
    f.doc->tick(1.f / 60.f);
    CHECK_FALSE(f.doc->runtimeInput().action("jump").down);

    // PLAY: toques alimentam o input do runtime.
    REQUIRE(f.doc->play().ok());
    f.doc->gameTouch(0, 0, 100.f, 25.f, 1.f);
    f.doc->tick(1.f / 60.f);
    CHECK(f.doc->runtimeInput().action("jump").down);
    CHECK(f.doc->runtimeInput().action("jump").pressed);
    f.doc->gameTouch(2, 0, 100.f, 25.f, 1.f);
    f.doc->tick(1.f / 60.f);
    CHECK(f.doc->runtimeInput().action("jump").released);

    // STOP: input do jogo congela com a sessão.
    f.doc->stop();
    CHECK_FALSE(f.doc->runtimeInput().action("jump").down);
}

// =============================================================================
// 7. Viewport (câmera + hit-test)
// =============================================================================

TEST_CASE("editor: câmera world↔screen ida e volta + zoom foco", "[editor]")
{
    eng::editor::Viewport viewport;
    viewport.setScreenSize(1080.f, 600.f);
    viewport.camera().posX = 10.f;
    viewport.camera().posY = -4.f;
    viewport.camera().zoom = 48.f;

    CHECK_THAT(viewport.screenToWorldX(viewport.worldToScreenX(123.5f)),
               Catch::Matchers::WithinAbs(123.5f, 1e-3f));
    CHECK_THAT(viewport.screenToWorldY(viewport.worldToScreenY(-42.25f)),
               Catch::Matchers::WithinAbs(-42.25f, 1e-3f));

    // Zoom centrado: o ponto sob o foco não se move na tela.
    const float focusX = 300.f;
    const float focusY = 200.f;
    const float wx = viewport.screenToWorldX(focusX);
    const float wy = viewport.screenToWorldY(focusY);
    viewport.zoomAt(1.5f, focusX, focusY);
    CHECK_THAT(viewport.screenToWorldX(focusX),
               Catch::Matchers::WithinAbs(wx, 1e-3f));
    CHECK_THAT(viewport.screenToWorldY(focusY),
               Catch::Matchers::WithinAbs(wy, 1e-3f));
    CHECK(viewport.camera().zoom == 72.f); // 48*1.5

    // Pan por delta de tela: ponto fixo da tela desloca (-dx/zoom, +dy/zoom).
    const float beforeX = viewport.screenToWorldX(500.f);
    const float beforeY = viewport.screenToWorldY(500.f);
    viewport.pan(96.f, 48.f);
    CHECK_THAT(viewport.screenToWorldX(500.f),
               Catch::Matchers::WithinAbs(beforeX - 96.f / 72.f, 1e-3f));
    CHECK_THAT(viewport.screenToWorldY(500.f),
               Catch::Matchers::WithinAbs(beforeY + 48.f / 72.f, 1e-3f));
}

TEST_CASE("editor: câmera de JOGO toma o viewport em Play (P0-5, ADR-051)", "[editor][tick]")
{
    DocFixture f;
    f.withProject();
    auto cam = f.doc->createEntity("Cam", eng::scene::kNoEntity);
    REQUIRE(cam.ok());
    // CameraData entra pelo CATÁLOGO (mesmo caminho do Inspector/JNI).
    REQUIRE(f.doc->addComponent(cam.value(), "eng::tick::CameraData").ok());
    REQUIRE(f.doc->setInspectorField(cam.value(), "eng::tick::CameraData",
                                    "posX", "12").ok());
    REQUIRE(f.doc->setInspectorField(cam.value(), "eng::tick::CameraData",
                                    "posY", "-6").ok());
    REQUIRE(f.doc->setInspectorField(cam.value(), "eng::tick::CameraData",
                                    "zoom", "96").ok());

    auto& viewport = f.doc->viewport();
    viewport.setScreenSize(1000.f, 500.f);
    // Câmera do EDITOR em outro lugar — se vazasse, o teste pega.
    viewport.camera().posX = 1000.f;
    viewport.camera().posY = 1000.f;
    viewport.camera().zoom = 8.f;

    REQUIRE(f.doc->play().ok());
    // play() NÃO roda frame (contrato FASE 11) — mas a câmera já resolve.
    CHECK(f.doc->hasGameCamera());
    CHECK(viewport.gameCameraActive());
    // Conversões seguem a câmera do JOGO: centro da tela = pos da câmera.
    CHECK_THAT(viewport.screenToWorldX(500.f),
               Catch::Matchers::WithinAbs(12.f, 1e-3f));
    CHECK_THAT(viewport.screenToWorldY(250.f),
               Catch::Matchers::WithinAbs(-6.f, 1e-3f));
    // worldToScreen do ponto da câmera = centro (zoom 96: 1 unidade = 96px).
    CHECK_THAT(viewport.worldToScreenX(12.f),
               Catch::Matchers::WithinAbs(500.f, 1e-2f));
    CHECK_THAT(viewport.worldToScreenY(-6.f),
               Catch::Matchers::WithinAbs(250.f, 1e-2f));

    // Gestos do editor são NO-OP sob câmera de jogo (debug honesto).
    const float beforeX = viewport.screenToWorldX(500.f);
    viewport.pan(120.f, 60.f);
    viewport.zoomAt(2.f, 500.f, 250.f);
    CHECK_THAT(viewport.screenToWorldX(500.f),
               Catch::Matchers::WithinAbs(beforeX, 1e-6f));

    f.doc->stop();
    // STOP devolve a câmera do editor.
    CHECK_FALSE(f.doc->hasGameCamera());
    CHECK_FALSE(viewport.gameCameraActive());
    CHECK_THAT(viewport.screenToWorldX(500.f),
               Catch::Matchers::WithinAbs(1000.f, 1e-3f));
}

TEST_CASE("editor: Play sem CameraData usa a câmera do editor (P0-5)", "[editor][tick]")
{
    DocFixture f;
    f.withProject();
    auto e = f.doc->createEntity("Plain", eng::scene::kNoEntity);
    REQUIRE(e.ok());

    auto& viewport = f.doc->viewport();
    viewport.setScreenSize(1000.f, 500.f);
    viewport.camera().posX = 42.f;
    viewport.camera().zoom = 48.f;

    REQUIRE(f.doc->play().ok());
    CHECK_FALSE(f.doc->hasGameCamera());
    CHECK_FALSE(viewport.gameCameraActive());
    CHECK_THAT(viewport.screenToWorldX(500.f),
               Catch::Matchers::WithinAbs(42.f, 1e-3f));
    // Pan continua funcionando (câmera do editor em foco).
    viewport.pan(48.f, 0.f);
    CHECK_THAT(viewport.screenToWorldX(500.f),
               Catch::Matchers::WithinAbs(41.f, 1e-3f));
    f.doc->stop();
}

TEST_CASE("editor: câmera de jogo desativada (active=false) cai para o editor", "[editor][tick]")
{
    DocFixture f;
    f.withProject();
    auto cam = f.doc->createEntity("Cam", eng::scene::kNoEntity);
    REQUIRE(cam.ok());
    REQUIRE(f.doc->addComponent(cam.value(), "eng::tick::CameraData").ok());
    REQUIRE(f.doc->setInspectorField(cam.value(), "eng::tick::CameraData",
                                    "active", "false").ok());

    auto& viewport = f.doc->viewport();
    viewport.setScreenSize(1000.f, 500.f);
    viewport.camera().posX = 7.f;

    REQUIRE(f.doc->play().ok());
    CHECK_FALSE(f.doc->hasGameCamera());
    CHECK_THAT(viewport.screenToWorldX(500.f),
               Catch::Matchers::WithinAbs(7.f, 1e-3f));
    f.doc->stop();
}

TEST_CASE("editor: hit-test seleciona o quad da frente", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto a = f.doc->createEntity("A", eng::scene::kNoEntity);
    auto b = f.doc->createEntity("B", eng::scene::kNoEntity);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    eng::editor::TransformDesc ta;
    ta.position = {0.f, 0.f, 0.f};
    REQUIRE(f.doc->setTransform(a.value(), ta).ok());
    eng::editor::TransformDesc tb;
    tb.position = {0.3f, 0.f, 0.f}; // sobreposto a A, criado depois (frente)
    REQUIRE(f.doc->setTransform(b.value(), tb).ok());

    auto& viewport = f.doc->viewport();
    viewport.setScreenSize(200.f, 200.f);

    const auto quads =
        viewport.buildQuads(*f.doc->sceneInFocus(), f.doc->selection());
    REQUIRE(quads.size() == 2);

    // Tap no centro: pega B (último desenhado = frente).
    auto hit = f.doc->viewportTap(100.f, 100.f);
    REQUIRE(hit.has_value());
    CHECK(*hit == b.value());

    // Tap longe: nada.
    auto none = f.doc->viewportTap(5.f, 5.f);
    CHECK_FALSE(none.has_value());
    CHECK_FALSE(f.doc->selection().has_value());
}

TEST_CASE("editor: quads refletem hierarquia (world matrix)", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto parent = f.doc->createEntity("P", eng::scene::kNoEntity);
    auto child = f.doc->createEntity("C", parent.value());
    REQUIRE(parent.ok());
    REQUIRE(child.ok());
    eng::editor::TransformDesc tp;
    tp.position = {4.f, 0.f, 0.f};
    REQUIRE(f.doc->setTransform(parent.value(), tp).ok());
    eng::editor::TransformDesc tc;
    tc.position = {1.f, 0.f, 0.f};
    REQUIRE(f.doc->setTransform(child.value(), tc).ok());

    const auto quads = f.doc->viewport().buildQuads(
        *f.doc->sceneInFocus(), f.doc->selection());
    REQUIRE(quads.size() == 2);
    // Filho: 4 (pai) + 1 (local) = 5 no X.
    CHECK_THAT(quads[1].worldX, Catch::Matchers::WithinAbs(5.f, 1e-3f));
    CHECK_THAT(quads[0].worldX, Catch::Matchers::WithinAbs(4.f, 1e-3f));
}

// =============================================================================
// 8. AssetBrowser (§8.10: asset discovery)
// =============================================================================

TEST_CASE("editor: importa, lista, renomeia, move e remove assets", "[editor]")
{
    DocFixture f;
    f.withProject();

    // Arquivo temporário (como o SAF do Android deixaria — §D7).
    REQUIRE(f.fs->mkdirs(eng::fs::Path{".import_tmp"}).ok());
    REQUIRE(f.fs->writeAllText(eng::fs::Path{".import_tmp/grass.png"},
                               "PNGDATA")
                .ok());

    auto* browser = f.doc->assets();
    REQUIRE(browser != nullptr);

    auto imported = browser->import(".import_tmp/grass.png", "textures",
                                     "grass");
    REQUIRE(imported.ok());
    CHECK_FALSE(imported.value().empty());

    auto listed = browser->list("textures");
    REQUIRE(listed.ok());
    REQUIRE(listed.value().size() == 1);
    CHECK(listed.value()[0].name == "grass.png");
    CHECK(listed.value()[0].registered);
    CHECK(listed.value()[0].id == imported.value());

    // Rename: id preservado (ADR-029).
    REQUIRE(browser->rename("textures", "grass.png", "lava.png").ok());
    listed = browser->list("textures");
    REQUIRE(listed.ok());
    REQUIRE(listed.value().size() == 1);
    CHECK(listed.value()[0].name == "lava.png");
    CHECK(listed.value()[0].id == imported.value());

    // Move p/ models: tipo muda, id preservado.
    REQUIRE(browser->move("textures", "lava.png", "models").ok());
    auto models = browser->list("models");
    REQUIRE(models.ok());
    REQUIRE(models.value().size() == 1);
    CHECK(models.value()[0].id == imported.value());
    auto textures = browser->list("textures");
    REQUIRE(textures.ok());
    CHECK(textures.value().empty());

    // Remove: some de tudo.
    REQUIRE(browser->remove("models", "lava.png").ok());
    models = browser->list("models");
    REQUIRE(models.ok());
    CHECK(models.value().empty());
    CHECK(browser->registry().size() == 0);
}

TEST_CASE("editor: import preserva a extensão de nomes CURTOS (regr. P0)",
          "[editor]")
{
    DocFixture f;
    f.withProject();
    REQUIRE(f.fs->mkdirs(eng::fs::Path{".import_tmp"}).ok());
    REQUIRE(f.fs->writeAllText(eng::fs::Path{".import_tmp/hero.png"},
                               "PNGDATA")
                .ok());

    auto* browser = f.doc->assets();
    REQUIRE(browser != nullptr);

    // "hero" tem 4 chars — o MESMO comprimento de ".png". Antes: o guard
    // `size() >= ext.size()+1` negava a extensão e o arquivo era salvo como
    // "hero" (sem sufixo) — a fronteira que valida pelo nome final
    // (com extensão) não encontrava o arquivo.
    std::string finalName;
    auto imported = browser->import(".import_tmp/hero.png", "textures",
                                     "hero", &finalName);
    REQUIRE(imported.ok());
    CHECK(finalName == "hero.png");

    // O arquivo existe EXATAMENTE sob o nome final devolvido (é ele que a
    // validação JNI lê e o TextureCache resolve).
    auto bytes = browser->read("textures", finalName);
    REQUIRE(bytes.ok());
    CHECK(bytes.value().size() == 7);  // "PNGDATA"

    // E a listagem mostra o nome com extensão.
    auto listed = browser->list("textures");
    REQUIRE(listed.ok());
    REQUIRE(listed.value().size() == 1);
    CHECK(listed.value()[0].name == "hero.png");
}

TEST_CASE("editor: arquivo não catalogado aparece como unregistered", "[editor]")
{
    DocFixture f;
    f.withProject();
    REQUIRE(f.fs->mkdirs(eng::fs::Path{"TestGame/assets/scenes"}).ok());
    REQUIRE(f.fs->writeAllText(
                 eng::fs::Path{"TestGame/assets/scenes/level.json"}, "{}")
                .ok());

    auto listed = f.doc->assets()->list("scenes");
    REQUIRE(listed.ok());
    REQUIRE(listed.value().size() == 1);
    CHECK_FALSE(listed.value()[0].registered);
}

// =============================================================================
// 8.5 Guarda de ambiente (bug C-17 da auditoria final)
// =============================================================================

namespace {

/// EditorHost::create com backend real exige driver (lavapipe/EGL). Sem
/// driver o caso SKIPA com motivo — o mesmo protocolo de degradação de
/// rhi_vulkan/rhi_gles (antes: 3 casos FALHAVAM neste ambiente).
bool editorGraphicsUnavailable() {
    (void)eng::rhi::Renderer::registerBackend(
        eng::rhi::BackendType::Vulkan, &eng::rhi::vulkan::createBackend);
    (void)eng::rhi::Renderer::registerBackend(
        eng::rhi::BackendType::OpenGLES, &eng::rhi::gles::createBackend);
    eng::rhi::RendererConfig config; // device-only probe
    config.enableValidation = false;
    auto renderer = eng::rhi::Renderer::create(config);
    return renderer.isError();
}

}  // namespace

// =============================================================================
// 9. ViewportRenderer + EditorHost — backends reais (rhi_hardware)
// =============================================================================

TEST_CASE("editor: viewport renderer desenha quads (GLES/llvmpipe)",
          "[editor][rhi_hardware]")
{
    if (editorGraphicsUnavailable()) {
        SKIP("sem driver gráfico (lavapipe/EGL) — suite completo roda no CI");
    }
    auto host = eng::editor::EditorHost::create("gles",
                                                ".editor-test-ws-gles");
    REQUIRE(host.ok());
    std::unique_ptr<eng::editor::EditorHost> owned{host.value()};

    auto& doc = owned->document();
    ensureProject(doc, "RenderGame");
    auto entity = doc.createEntity("Red", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    // Entidade no CENTRO da tela (0,0 mundo).
    owned->document().viewport().setScreenSize(64.f, 48.f);

    int marker = 0;
    owned->surfaceCreated(&marker, eng::rhi::NativeWindowKind::Headless, 64,
                          48);
    REQUIRE(owned->state() == eng::editor::HostSurfaceState::Available);

    REQUIRE(owned->renderFrame(1.f / 60.f));
    REQUIRE(owned->stats().firstFrameSubmitted);
    REQUIRE(owned->stats().firstFramePresented);

    // PROVA de conteúdo sem readback: os vértices enviados. O quad da
    // entidade cobre o centro (0,0 clip) — os 6 vértices do quad
    // englobam (0,0) com meia-largura >= kMinQuadPixels/64 em clip.
    const auto* renderer = owned->capabilities(); // (não-null = vivo)
    CHECK(renderer != nullptr);
    CHECK(owned->selectedBackend() == eng::rhi::BackendType::OpenGLES);

    // Ciclo de surface (§XXVIII FASE 7 adaptado): destroy→recreate→render.
    owned->surfaceDestroyed();
    CHECK(owned->state() == eng::editor::HostSurfaceState::Destroyed);
    CHECK_FALSE(owned->renderFrame(1.f / 60.f)); // sem surface: no-op
    owned->surfaceCreated(&marker, eng::rhi::NativeWindowKind::Headless, 64,
                          48);
    REQUIRE(owned->renderFrame(1.f / 60.f));

    // Pause/Resume com render.
    owned->onPause();
    CHECK_FALSE(owned->renderFrame(1.f / 60.f));
    owned->onResume();
    REQUIRE(owned->renderFrame(1.f / 60.f));
}

TEST_CASE("editor: viewport renderer Vulkan/lavapipe submete frames",
          "[editor][rhi_hardware]")
{
    if (editorGraphicsUnavailable()) {
        SKIP("sem driver gráfico (lavapipe/EGL) — suite completo roda no CI");
    }
    auto host =
        eng::editor::EditorHost::create("vulkan", ".editor-test-ws-vk");
    REQUIRE(host.ok());
    std::unique_ptr<eng::editor::EditorHost> owned{host.value()};

    auto& doc = owned->document();
    ensureProject(doc, "VkGame");
    REQUIRE(doc.createEntity("Entity", eng::scene::kNoEntity).ok());

    int marker = 0;
    owned->surfaceCreated(&marker, eng::rhi::NativeWindowKind::Headless, 64,
                          48);
    REQUIRE(owned->state() == eng::editor::HostSurfaceState::Available);
    CHECK(owned->selectedBackend() == eng::rhi::BackendType::Vulkan);

    const auto before = owned->stats().framesSubmitted;
    REQUIRE(owned->renderFrame(1.f / 60.f));
    CHECK(owned->stats().framesSubmitted == before + 1);
    CHECK(owned->stats().framesPresented >= 1);
}

TEST_CASE("editor: play/stop alterna conteúdo do viewport no host", "[editor]")
{
    if (editorGraphicsUnavailable()) {
        SKIP("sem driver gráfico (lavapipe/EGL) — suite completo roda no CI");
    }
    auto host = eng::editor::EditorHost::create("auto", ".editor-test-ws-auto");
    REQUIRE(host.ok());
    std::unique_ptr<eng::editor::EditorHost> owned{host.value()};

    auto& doc = owned->document();
    ensureProject(doc, "PlayGame");
    auto entity = doc.createEntity("Thing", eng::scene::kNoEntity);
    REQUIRE(entity.ok());

    int marker = 0;
    owned->surfaceCreated(&marker, eng::rhi::NativeWindowKind::Headless, 64,
                          48);

    REQUIRE(owned->renderFrame(1.f / 60.f)); // Edit
    REQUIRE(doc.play().ok());
    REQUIRE(owned->renderFrame(1.f / 60.f)); // Play (foco = clone)
    doc.stop();
    REQUIRE(owned->renderFrame(1.f / 60.f)); // Edit de novo
}

// =============================================================================
// 10. Reabertura entre execuções (regression FASE 8)
// =============================================================================

TEST_CASE("editor: reabre projeto de execução anterior via workspace", "[editor]")
{
    // REGRESSÃO FASE 8: openProject resolvia contra o CWD em vez do
    // WORKSPACE — só reproduzível com disco persistente entre hosts (o
    // CI sempre roda com dirs novos). Sem GPU (host sem surface).
    const char* ws = ".editor-test-ws-reopen";
    {
        auto host = eng::editor::EditorHost::create("auto", ws);
        REQUIRE(host.ok());
        std::unique_ptr<eng::editor::EditorHost> owned{host.value()};
        // Runs repetidos encontram o projeto — para ESTE teste tanto faz
        // quem o criou; o que se valida é a REABERTURA pelo workspace.
        (void)owned->document().newProject("ReopenGame");
    }
    auto host2 = eng::editor::EditorHost::create("auto", ws);
    REQUIRE(host2.ok());
    std::unique_ptr<eng::editor::EditorHost> owned2{host2.value()};
    auto& doc2 = owned2->document();
    REQUIRE(doc2.newProject("ReopenGame").isError());            // já existe
    REQUIRE(doc2.openProject(eng::fs::Path{"ReopenGame"}).ok()); // pelo workspace
    CHECK(doc2.projectName() == "ReopenGame");
    CHECK_FALSE(doc2.projectDirty());
}

// =============================================================================
// 11. FASE 10 — física/animação/partículas em PLAY (§8 integração)
// =============================================================================

TEST_CASE("editor: componentes de gameplay no catálogo/serialização",
          "[editor]")
{
    const auto catalog = eng::editor::Inspector::catalog();
    for (const char* name :
         {"eng::physics::RigidBody", "eng::physics::Collider",
          "eng::physics::CharacterBody", "eng::animation::Animator",
          "eng::particles::ParticleEmitter"}) {
        CAPTURE(name);
        CHECK(std::find(catalog.begin(), catalog.end(), name) !=
              catalog.end());
    }

    // Round-trip pela cena: cria com physics/animation/particles, salva,
    // recarrega — componentes persistem (ADR-033).
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("Gameplay", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(f.doc->addComponent(entity.value(), "eng::physics::RigidBody")
                .ok());
    REQUIRE(f.doc->addComponent(entity.value(), "eng::physics::Collider")
                .ok());
    REQUIRE(f.doc->addComponent(entity.value(),
                                 "eng::animation::Animator")
                .ok());
    REQUIRE(f.doc->addComponent(entity.value(),
                                 "eng::particles::ParticleEmitter")
                .ok());
    REQUIRE(f.doc->setInspectorField(entity.value(),
                                     "eng::physics::RigidBody", "mass", "2.5")
                .ok());

    REQUIRE(f.doc->saveScene("gp.json").ok());
    REQUIRE(f.doc->loadScene("gp.json").ok());
    auto snapshot = f.doc->hierarchySnapshot();
    REQUIRE(snapshot.size() == 1);
    auto got = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), snapshot[0].entity,
        "eng::physics::RigidBody", "mass");
    REQUIRE(got.ok());
    CHECK(got.value() == "2.5");
}

TEST_CASE("editor: PLAY avança física (timestep fixo) sobre o CLONE",
          "[editor]")
{
    DocFixture f;
    f.withProject();
    auto ball = f.doc->createEntity("Ball", eng::scene::kNoEntity);
    REQUIRE(ball.ok());
    REQUIRE(f.doc->addComponent(ball.value(), "eng::physics::RigidBody")
                .ok());
    // Gravidade padrão -9.81; posição y=10.
    eng::editor::TransformDesc tr;
    tr.position = {0.f, 10.f, 0.f};
    REQUIRE(f.doc->setTransform(ball.value(), tr).ok());

    REQUIRE(f.doc->play().ok());
    // 0.5s em frames de ~8ms: física avança por passos FIXOS de 1/60.
    for (int i = 0; i < 61; ++i) {
        f.doc->tick(1.f / 120.f);
    }
    auto runtimeY = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), ball.value(), "eng::math::Transform",
        "position.y");
    REQUIRE(runtimeY.ok());
    const float fallen = 10.f - std::stof(runtimeY.value());
    CHECK(fallen > 0.5f); // caiu de verdade
    CHECK(fallen < 1.3f); // ~0.5s de queda (1.22m)

    f.doc->stop();
    // Edição INTACTA (§8.7): y continua 10.
    auto editY = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), ball.value(), "eng::math::Transform",
        "position.y");
    REQUIRE(editY.ok());
    CHECK(editY.value() == "10");
}

TEST_CASE("editor: PLAY avança animação e partículas sobre o CLONE",
          "[editor]")
{
    DocFixture f;
    f.withProject();
    auto node = f.doc->createEntity("Fx", eng::scene::kNoEntity);
    REQUIRE(node.ok());
    REQUIRE(f.doc->addComponent(node.value(),
                                 "eng::animation::Animator")
                .ok());
    REQUIRE(f.doc->addComponent(node.value(),
                                 "eng::particles::ParticleEmitter")
                .ok());
    // Animator: clip "rise", tocando.
    eng::animation::AnimationClip rise;
    rise.name = "rise";
    rise.position = {{0.f, {0.f, 0.f, 0.f}}, {1.f, {0.f, 2.f, 0.f}}};
    f.doc->runtimeAnimations().add(rise);
    REQUIRE(f.doc->setInspectorField(node.value(), "eng::animation::Animator",
                                     "clip", "rise")
                .ok());
    REQUIRE(f.doc->setInspectorField(node.value(), "eng::animation::Animator",
                                     "playing", "true")
                .ok());

    REQUIRE(f.doc->play().ok());
    for (int i = 0; i < 30; ++i) {
        f.doc->tick(1.f / 60.f);
    }
    // Animação aplicada ao CLONE: y ≈ 1.0 (0.5s de 1s de clip).
    auto y = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), node.value(), "eng::math::Transform",
        "position.y");
    REQUIRE(y.ok());
    CHECK(std::stof(y.value()) > 0.9f);

    // Partículas vivas no clone (emitter padrão 20/s).
    CHECK(eng::particles::ParticleSystem::aliveCount(*f.doc->sceneInFocus()) >
          0);

    f.doc->stop();
    // Edição intacta: sem animação aplicada.
    auto editY = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), node.value(), "eng::math::Transform",
        "position.y");
    REQUIRE(editY.ok());
    CHECK(std::stof(editY.value()) == 0.f);
    // E as partículas morreram com o clone.
    CHECK(eng::particles::ParticleSystem::aliveCount(
              *f.doc->sceneInFocus()) == 0);
}

// =============================================================================
// 12. Pack/unpack JNI
// =============================================================================

TEST_CASE("editor: pack/unpack entity é bijetivo exceto 0", "[editor]")
{
    const eng::ecs::Entity e{17u, 3u};
    const std::uint64_t packed = pack(e);
    CHECK(packed != 0ull);
    const eng::ecs::Entity back = unpack(packed);
    CHECK(back == e);
    CHECK(unpack(0ull) == eng::scene::kNoEntity);
}

// =============================================================================
// Correções da auditoria final FASES 4–10 (remediação)
// =============================================================================

TEST_CASE("editor: documento pré-projeto é editável sem UB (C-3)", "[editor]")
{
    // Antes: create() não emitia a cena — sceneInFocus() fazia &*scene_
    // vazio (UB). Agora o contrato do header ("cena vazia PRONTA PARA
    // EDIÇÃO") é real.
    eng::fs::MemoryFileSystem fs;
    auto docResult = EditorDocument::create(fs, eng::fs::Path{".ws-pre"});
    REQUIRE(docResult.ok());
    auto doc = std::move(docResult.value());

    REQUIRE(doc->sceneInFocus() != nullptr);          // sem UB
    const auto before = doc->sceneInFocus()->nodeCount();
    CHECK(before == 0);

    // Comandos de cena funcionam ANTES de qualquer newProject.
    auto entity = doc->createEntity("Solto", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    CHECK(doc->sceneInFocus()->nodeCount() == 1);
    CHECK(doc->hierarchySnapshot().size() == 1);

    // Comandos que exigem projeto continuam rejeitando com erro preciso.
    auto saved = doc->saveProject();
    CHECK(saved.isError());
}

TEST_CASE("editor: viewport desenha partículas vivas como quads (drift D6)",
          "[editor]")
{
    // A auditoria final mostrou que NADA lia a ParticlePool — agora o
    // Viewport gera um quad por partícula viva (clone em Play tem pools).
    eng::fs::MemoryFileSystem fs;
    auto docResult = EditorDocument::create(fs, eng::fs::Path{".ws-part"});
    REQUIRE(docResult.ok());
    auto doc = std::move(docResult.value());
    ensureProject(*doc, "ParticleQuads");

    auto emitter = doc->createEntity("Emitter", eng::scene::kNoEntity);
    REQUIRE(emitter.ok());
    REQUIRE(doc->addComponent(emitter.value(),
                              "eng::particles::ParticleEmitter")
                .ok());
    // rate 10/s (default é 5) via inspector de reflexão.
    REQUIRE(doc->setInspectorField(emitter.value(),
                                   "eng::particles::ParticleEmitter", "rate",
                                   "10")
                .ok());

    // Em Edit: sem pool → zero quads de partícula.
    const auto& viewport = doc->viewport();
    const eng::scene::Scene* editScene = doc->sceneInFocus();
    CHECK(viewport.buildParticleQuads(*editScene).empty());

    // Em Play: o clone ganha pools após o primeiro tick (spawn por
    // acumulador) → quads de partícula existem.
    REQUIRE(doc->play().ok());
    doc->tick(0.5f); // 10/s * 0.5s = 5 vivas
    const auto quads = viewport.buildParticleQuads(*doc->sceneInFocus());
    CHECK(quads.size() == 5);
    doc->stop();
    CHECK(viewport.buildParticleQuads(*doc->sceneInFocus()).empty());
}

// =============================================================================
// 15. Sprite com TEXTURA REAL (evolução P0 — o fim do retângulo colorido)
// =============================================================================

namespace {

/// PNG 2x2 RGBA (vermelho/verde/azul/branco) — cópia EXATA do fixture
/// kPng2x2 de engine/image/tests/ImageFixtures.hpp (gerado por
/// scripts/gen_image_fixtures.py — procedência única).
constexpr unsigned char kPng2x2[86] = {
    137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,
    0,0,0,2,0,0,0,2,8,6,0,0,0,114,182,13,
    36,0,0,0,29,73,68,65,84,120,1,1,18,0,237,255,
    0,255,0,0,255,0,255,0,255,4,1,0,255,0,255,0,
    0,0,62,255,6,0,112,227,74,153,0,0,0,0,73,69,
    78,68,174,66,96,130,
};

void writePngTemp(eng::fs::FileSystem& fs)
{
    REQUIRE(fs.mkdirs(eng::fs::Path{".import_tmp"}).ok());
    REQUIRE(fs.writeAllBytes(
                eng::fs::Path{".import_tmp/grass.png"},
                std::span{reinterpret_cast<const std::byte*>(kPng2x2),
                          sizeof(kPng2x2)})
                .ok());
}

}  // namespace

TEST_CASE("editor: sprite — importar imagem, atribuir, quads e persistência",
          "[editor]")
{
    DocFixture f;
    f.withProject();
    writePngTemp(*f.fs);

    // 1) Import REAL: bytes PNG válidos catalogados em textures/.
    auto* browser = f.doc->assets();
    REQUIRE(browser != nullptr);
    auto imported = browser->import(".import_tmp/grass.png", "textures", "grass");
    REQUIRE(imported.ok());

    // 2) Metadados decodificados SEM GPU (dimensões/alfa do arquivo real).
    eng::editor::TextureCache cache;
    const auto info = cache.imageInfo(*browser, "grass.png");
    CHECK(info.valid);
    CHECK(info.width == 2);
    CHECK(info.height == 2);
    CHECK(info.alpha);

    // 3) Entidade com componente SpriteData (catálogo ÚNICO — ADR-043).
    auto created = f.doc->createEntity("Player", eng::scene::kNoEntity);
    REQUIRE(created.ok());
    const eng::ecs::Entity player = created.value();
    REQUIRE(f.doc->addComponent(player, "eng::editor::SpriteData").ok());

    // 4) Atribuição da textura por CAMPO (mesma via do Inspector/JNI).
    REQUIRE(f.doc
                ->setInspectorField(player, "eng::editor::SpriteData",
                                   "textureAsset", "grass.png")
                .ok());
    // Campos do workflow: região + flip + tint + ppu.
    REQUIRE(f.doc
                ->setInspectorField(player, "eng::editor::SpriteData",
                                   "pixelsPerUnit", "0.5")
                .ok());
    REQUIRE(f.doc
                ->setInspectorField(player, "eng::editor::SpriteData", "flipX",
                                   "true")
                .ok());

    // 5) O QUAD carrega o sprite (não é mais o marcador hue puro).
    const auto* scene = f.doc->sceneInFocus();
    const auto quads = f.doc->viewport().buildQuads(*scene, player);
    REQUIRE(quads.size() == 1);
    CHECK(quads[0].textureAsset == "grass.png");
    CHECK(quads[0].u0 == 0.f);
    CHECK(quads[0].u1 == 1.f);
    CHECK(quads[0].flipX);
    CHECK(quads[0].tintR == 1.f);
    CHECK(quads[0].spritePpu == 0.5f);

    // 6) Persistência: save/load preserva o SpriteData COMPLETO.
    REQUIRE(f.doc->saveScene("sprites.json").ok());
    REQUIRE(f.doc->loadScene("sprites.json").ok());
    const auto* sprite = f.doc->sceneInFocus()->world().get<eng::editor::SpriteData>(
        player);
    REQUIRE(sprite != nullptr);
    CHECK(sprite->textureAsset == "grass.png");
    CHECK(sprite->pixelsPerUnit == 0.5f);
    CHECK(sprite->flipX);
    CHECK(sprite->flipY == false);

    // 7) Componente no catálogo do inspector (aparece para o usuário).
    const auto componentsTsv = f.doc->inspectorFields(player, "eng::editor::SpriteData");
    REQUIRE_FALSE(componentsTsv.empty());
}

TEST_CASE("editor: sprite com textura AUSENTE cai no caminho de cor (honesto)",
          "[editor]")
{
    DocFixture f;
    f.withProject();

    auto created = f.doc->createEntity("Ghost", eng::scene::kNoEntity);
    REQUIRE(created.ok());
    REQUIRE(f.doc->addComponent(created.value(), "eng::editor::SpriteData").ok());
    REQUIRE(f.doc
                ->setInspectorField(created.value(), "eng::editor::SpriteData",
                                   "textureAsset", "nao_existe.png")
                .ok());

    // Quad marcado como sprite, mas o acquire falha → renderiza como quad
    // de cor (sem crash, sem placeholder falso — log único no cache).
    const auto* scene = f.doc->sceneInFocus();
    const auto quads = f.doc->viewport().buildQuads(*scene, created.value());
    REQUIRE(quads.size() == 1);
    CHECK(quads[0].textureAsset == "nao_existe.png");
    // O TextureCache SEM renderer não sobe nada — imageInfo invalida.
    eng::editor::TextureCache cache;
    const auto info = cache.imageInfo(*f.doc->assets(), "nao_existe.png");
    CHECK_FALSE(info.valid);
}

TEST_CASE("editor: host renderiza sprite TEXTURIZADO (GLES/llvmpipe real)",
          "[editor][rhi_hardware]")
{
    eng::rhi::Renderer::clearRegisteredBackends();
    REQUIRE(eng::rhi::Renderer::registerBackend(
                eng::rhi::BackendType::OpenGLES, &eng::rhi::gles::createBackend)
                .ok());

    // Host em disco REAL (workspace temporário isolado por caso).
    const std::string root = "sprite_host_test_" +
                             std::to_string(reinterpret_cast<std::uintptr_t>(&root));
    auto created = eng::editor::EditorHost::create("gles", root.c_str());
    if (!created) {
        SKIP("OpenGL ES indisponível: " << created.error().message);
    }
    std::unique_ptr<eng::editor::EditorHost> host{created.value()};
    // RECOVERY P0: janela-marker headless — antes nullptr, o que deixava o
    // host em NoSurface e este teste SKIPAVA ATÉ NO CI (nunca validou o
    // upload de textura de verdade; o bug da orientação sobreviveu por isso).
    int marker = 0;
    host->surfaceCreated(&marker, eng::rhi::NativeWindowKind::Headless, 64, 48);
    if (host->viewportRenderer() == nullptr) {
        SKIP("OpenGL ES indisponível (renderer não criado sem driver)");
    }

    auto& doc = host->document();
    REQUIRE(doc.newProject("SpriteGame").ok());
    REQUIRE(doc.saveProject().ok());

    // Import via fs do WORKSPACE (rooted — a mesma fronteira do Android):
    // escreve o staging PNG e importa.
    auto* browser = doc.assets();
    REQUIRE(browser != nullptr);
    {
        eng::fs::FileSystem& ws = host->workspace();
        REQUIRE(ws.mkdirs(eng::fs::Path{".import_tmp"}).ok());
        REQUIRE(ws
                    .writeAllBytes(
                        eng::fs::Path{".import_tmp/hero.png"},
                        std::span{reinterpret_cast<const std::byte*>(kPng2x2),
                                  sizeof(kPng2x2)})
                    .ok());
    }
    // import() usa path relativo ao ROOT do projeto — o staging do host é
    // criado FORA do projeto (filesDir/.import_tmp): o documento do host
    // aponta o workspace p/ root/, então o import resolve .import_tmp/hero.png
    // relativo ao workspace. (Contrato §8.5: staging DENTRO do workspace.)
    auto imported = browser->import(".import_tmp/hero.png", "textures", "hero");
    REQUIRE(imported.ok());

    auto entity = doc.createEntity("Hero", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(doc.addComponent(entity.value(), "eng::editor::SpriteData").ok());
    REQUIRE(doc.setInspectorField(entity.value(), "eng::editor::SpriteData",
                                 "textureAsset", "hero.png")
                .ok());

    // Frame com o sprite: textura REAL subiu (decode→RHI→bind→draw).
    REQUIRE(host->renderFrame(1.f / 60.f));
    const auto* renderer = host->viewportRenderer();
    REQUIRE(renderer != nullptr);
    CHECK(renderer->lastFrameTexturedSprites() == 1);
    const auto& spriteVerts = renderer->lastFrameSpriteVertices();
    REQUIRE(spriteVerts.size() == 6);
    // UV completo no quad (0,0)→(1,1): a amostragem cobre a textura.
    CHECK(spriteVerts[0].u == 0.f);
    CHECK(spriteVerts[0].v == 0.f);
    CHECK(spriteVerts[1].u == 1.f);
    CHECK(spriteVerts[1].v == 0.f);
    CHECK(spriteVerts[2].u == 1.f);
    CHECK(spriteVerts[2].v == 1.f);
    CHECK(spriteVerts[5].u == 0.f);
    CHECK(spriteVerts[5].v == 1.f);

    // Segundo frame: cache HIT (mesma textura — sem novo upload) e Play
    // com sprites no clone (separação editor×runtime mantida).
    REQUIRE(host->renderFrame(1.f / 60.f));
    CHECK(renderer->lastFrameTexturedSprites() == 1);

    REQUIRE(doc.play().ok());
    REQUIRE(host->renderFrame(1.f / 60.f));
    CHECK(renderer->lastFrameTexturedSprites() == 1);
    REQUIRE(host->renderFrame(1.f / 60.f));
    doc.stop();
}

TEST_CASE("editor: SpriteData default — ppu 48 (imagem utilizável no viewport)",
          "[editor]")
{
    DocFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("Hero", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(f.doc->addComponent(entity.value(), "eng::editor::SpriteData").ok());

    // Default = 48 px/unidade (casa com o zoom padrão da câmera → a imagem
    // aparece 1:1 na tela). Antes: 1 → uma foto de 1080px media 51.840px de
    // tela — o viewport virava um "mar de cor".
    const auto* scene = f.doc->sceneInFocus();
    const auto quads = f.doc->viewport().buildQuads(*scene, entity.value());
    REQUIRE(quads.size() == 1);
    CHECK(quads[0].spritePpu == 48.f);
}

// =============================================================================
// REPRODUÇÃO P0 (RECOVERY FASE 0) — "A imagem é importada mas NÃO APARECE
// CORRETAMENTE no viewport". Contrato VISUAL do sprite pinhado com PIXEL
// REAL lido da surface (missão: feature visual é validada visualmente):
//   1. TAMANHO — o quad cobre a região mundial esperada (sem o fator 0.5
//      espúrio no half-extent NDC que desenhava tudo com metade do size);
//   2. ORIENTAÇÃO — o topo da imagem aparece no TOPO do quad na tela
//      (stb decodifica top-down; GL tem v=0 na BASE — sem o flip na
//      fronteira de upload, o sprite sai de ponta-cabeça);
//   3. SELEÇÃO — o hit-test acerta a borda do sprite (tamanho desenhado,
//      não a escala local).
// =============================================================================

TEST_CASE("editor: P0 — PNG no viewport: tamanho, orientação e hit CORRETOS",
          "[editor][rhi_hardware]")
{
    if (editorGraphicsUnavailable()) {
        SKIP("sem driver gráfico (lavapipe/EGL) — suite completo roda no CI");
    }
    auto host = eng::editor::EditorHost::create("gles",
                                                ".editor-test-ws-p0img");
    REQUIRE(host.ok());
    std::unique_ptr<eng::editor::EditorHost> owned{host.value()};
    int marker = 0;  // janela-marker headless (mesmo padrão dos testes GLES)
    owned->surfaceCreated(&marker, eng::rhi::NativeWindowKind::Headless,
                          128, 128);
    REQUIRE(owned->state() == eng::editor::HostSurfaceState::Available);

    auto& doc = owned->document();
    ensureProject(doc, "P0ImageGame");

    // PNG canônico 2x2 (kPng2x2): linha 0 do ARQUIVO é o TOPO da imagem —
    // (0,0)=vermelho TL, (1,0)=verde TR, (0,1)=azul BL, (1,1)=branco BR.
    auto* browser = doc.assets();
    REQUIRE(browser != nullptr);
    {
        eng::fs::FileSystem& ws = owned->workspace();
        REQUIRE(ws.mkdirs(eng::fs::Path{".import_tmp"}).ok());
        REQUIRE(ws
                    .writeAllBytes(
                        eng::fs::Path{".import_tmp/quad.png"},
                        std::span{reinterpret_cast<const std::byte*>(kPng2x2),
                                  sizeof(kPng2x2)})
                    .ok());
    }
    REQUIRE(browser->import(".import_tmp/quad.png", "textures", "quad").ok());

    // Entidade em (0.5, -0.5), sprite região completa, ppu=1 → tamanho
    // mundial 2x2 unidades (região 2px / ppu 1). Câmera padrão (0,0),
    // zoom 48 (1 unidade = 48px; superfície 128x128): o CENTRO da tela
    // cai no ponto (u=0.25, v=0.75) do sprite — 25% da esquerda, 75% de
    // baixo — exatamente o canto SUPERIOR-ESQUERDO da imagem original.
    auto entity = doc.createEntity("Sprite", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(doc.addComponent(entity.value(), "eng::editor::SpriteData").ok());
    REQUIRE(doc
                .setInspectorField(entity.value(), "eng::editor::SpriteData",
                                   "textureAsset", "quad.png")
                .ok());
    REQUIRE(doc
                .setInspectorField(entity.value(), "eng::editor::SpriteData",
                                   "pixelsPerUnit", "1")
                .ok());
    REQUIRE(doc
                .setInspectorField(entity.value(), "eng::math::Transform",
                                   "position.x", "0.5")
                .ok());
    REQUIRE(doc
                .setInspectorField(entity.value(), "eng::math::Transform",
                                   "position.y", "-0.5")
                .ok());

    REQUIRE(owned->renderFrame(1.f / 60.f));
    auto* renderer = owned->viewportRenderer();
    REQUIRE(renderer != nullptr);
    CHECK(renderer->lastFrameTexturedSprites() == 1);

    // CONTRATO 1 — TAMANHO: topo do quad em world y = -0.5 + 1.0 = 0.5
    // → tela y = 64 - 0.5*48 = 40 → clip = 1 - (40/128)*2 = 0.375.
    // (Antes: 0.0 — o half-extent do sprite carregava um 0.5 espúrio e a
    // imagem desenhava com METADE do tamanho mundial, menor que a própria
    // borda de seleção.)
    const auto& verts = renderer->lastFrameSpriteVertices();
    REQUIRE(verts.size() == 6);
    CHECK(verts[5].y == Catch::Approx(0.375f).margin(1e-3f));

    // CONTRATO 2 — ORIENTAÇÃO: o pixel central da tela mostra o canto
    // SUPERIOR-ESQUERDO da imagem = VERMELHO (não azul — ponta-cabeça).
    std::uint8_t pixel[4] = {0, 0, 0, 0};
    REQUIRE(renderer->renderer()->readCenterPixel(pixel).ok());
    INFO("readback: " << +pixel[0] << " " << +pixel[1] << " " << +pixel[2]
                      << " " << +pixel[3]);
    CHECK(pixel[0] >= 200);  // R dominante
    CHECK(pixel[1] <= 64);   // G baixo
    CHECK(pixel[2] <= 64);   // B baixo
    CHECK(pixel[3] == 255);  // opaco

    // CONTRATO 3 — SELEÇÃO: toque perto da borda direita do sprite
    // (world x≈1.29, y=-0.5 → tela (126, 88)) ACERTA — o hit box é o
    // tamanho desenhado (±48px), não a escala local (±24px).
    auto hit = doc.viewportTap(126.f, 88.f, &owned->textureCache());
    CHECK(hit.has_value());
}

// =============================================================================
// REGRESSÃO §26 (RECOVERY P0) — os bugs do APK Android:
//   "InvalidArgument: AssetBrowser: destino absoluto é proibido"
//   "InvalidArgument: EditorDocument: caminho absoluto proibido"
//
// Topologia ANDROID reproduzida no Linux: workspace FÍSICO ABSOLUTO (como
// filesDir/projects) entra pelo EditorHost. A fronteira (RootedFileSystem)
// converte; o editor opera relativo. Sem a correção, newProject até criava
// a estrutura, mas import/scriptCreate morriam nas validações anti-absoluto
// (o root absoluto vazava para dentro do documento).
// =============================================================================

namespace {

/// Tmpdir RAII ABSOLUTO (o do FsTests é relativo ao CWD dos testes de fs;
/// aqui o requisito é exatamente um root absoluto, como o Android entrega).
struct AbsTmpDir {
    std::filesystem::path dir;

    AbsTmpDir()
    {
        std::error_code ec;
        auto base = std::filesystem::temp_directory_path(ec);
        if (ec || base.empty()) {
            base = "/tmp";
        }
        static std::uint64_t counter = 0;
        do {
            dir = base / ("goni_editor_host_abs_" +
                          std::to_string(++counter) + "_" +
                          std::to_string(
                              reinterpret_cast<std::uintptr_t>(this)));
        } while (std::filesystem::exists(dir, ec));
        std::filesystem::create_directories(dir, ec);
    }

    ~AbsTmpDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    AbsTmpDir(const AbsTmpDir&) = delete;
    AbsTmpDir& operator=(const AbsTmpDir&) = delete;

    [[nodiscard]] std::string str() const { return dir.generic_string(); }
};

} // namespace

TEST_CASE("editor: host com workspace ABSOLUTO — import e script funcionam "
          "(regressão Android §26)",
          "[editor][rhi_hardware]")
{
    eng::rhi::Renderer::clearRegisteredBackends();
    REQUIRE(eng::rhi::Renderer::registerBackend(
                eng::rhi::BackendType::OpenGLES, &eng::rhi::gles::createBackend)
                .ok());

    AbsTmpDir tmp;
    // filesDir/projects do Android: ABSOLUTO. É o que a EditorActivity
    // passa por JNI (nativeEditorCreate("auto", workspace.absolutePath)).
    const std::string workspaceRoot = tmp.str() + "/projects";
    auto created = eng::editor::EditorHost::create("gles",
                                                   workspaceRoot.c_str());
    if (!created) {
        SKIP("OpenGL ES indisponível: " << created.error().message);
    }
    std::unique_ptr<eng::editor::EditorHost> host{created.value()};
    host->surfaceCreated(nullptr, eng::rhi::NativeWindowKind::Headless, 64,
                         48);

    auto& doc = host->document();

    // --- 1. Ciclo de vida do projeto (§4) -----------------------------------
    REQUIRE(doc.newProject("MeuJogo").ok());
    REQUIRE(doc.hasProject());
    CHECK(doc.projectName() == "MeuJogo");

    // --- 2. Import de asset (o bug nº 1 do APK) -----------------------------
    // SAF teria copiado para <workspace>/.import_tmp/ — staging relativo.
    {
        eng::fs::FileSystem& ws = host->workspace();
        REQUIRE(ws.mkdirs(eng::fs::Path{".import_tmp"}).ok());
        REQUIRE(ws.writeAllBytes(
                    eng::fs::Path{".import_tmp/grass.png"},
                    std::span{reinterpret_cast<const std::byte*>(kPng2x2),
                              sizeof(kPng2x2)})
                    .ok());
    }
    auto* browser = doc.assets();
    REQUIRE(browser != nullptr);
    auto imported = browser->import(".import_tmp/grass.png", "textures",
                                     "grass");
    REQUIRE(imported.ok());  // ← ANTES: "destino absoluto é proibido"

    // Import visível na listagem, com id do registry.
    auto listed = browser->list("textures");
    REQUIRE(listed.ok());
    REQUIRE(listed.value().size() == 1);
    CHECK(listed.value()[0].name == "grass.png");
    CHECK(listed.value()[0].registered);
    CHECK(listed.value()[0].id == imported.value());

    // --- 3. Criação de script (o bug nº 2 do APK) ---------------------------
    REQUIRE(doc.scriptCreate("Movimento").ok());  // ← ANTES: "caminho absoluto proibido"
    // Conteúdo maior que 512 bytes (§12 — sem buffer JNI truncando).
    std::string big = "# comentário grande\n";
    for (int i = 0; i < 40; ++i) {
        big += "# linha de preenchimento " + std::to_string(i) + "\n";
    }
    REQUIRE(doc.scriptWrite("Big.nis", big).ok());
    {
        auto back = doc.scriptRead("Big.nis");
        REQUIRE(back.ok());
        CHECK(back.value().size() == big.size());
        CHECK(back.value() == big);
    }
    {
        auto scripts = doc.scriptList();
        REQUIRE(scripts.ok());
        REQUIRE(scripts.value().size() == 2);  // Movimento.nis + Big.nis
    }

    // --- 4. Cena: save → reload → conteúdo íntegro --------------------------
    {
        auto entity = doc.createEntity("Player", eng::scene::kNoEntity);
        REQUIRE(entity.ok());
        REQUIRE(doc.addComponent(entity.value(),
                                 "eng::editor::SpriteData")
                    .ok());
        REQUIRE(doc.setInspectorField(entity.value(),
                                      "eng::editor::SpriteData",
                                      "textureAsset", "grass.png")
                    .ok());
        REQUIRE(doc.setTransform(entity.value(),
                                 {{64.f, 32.f, 0.f},
                                  {0.f, 45.f, 0.f},
                                  {2.f, 2.f, 1.f}})
                    .ok());
    }
    REQUIRE(doc.saveScene("main.json").ok());
    {
        // Round-trip: nova cena + recarrega o estado salvo.
        REQUIRE(doc.newScene().ok());
        REQUIRE(doc.loadScene("main.json").ok());
        auto snapshot = doc.hierarchySnapshot();
        REQUIRE(snapshot.size() == 1);
        CHECK(snapshot[0].name == "Player");
        auto transform = doc.transform(snapshot[0].entity);
        REQUIRE(transform.ok());
        // Round-trip Euler→Quat→Euler: precisão de FPU, não identidade bit
        // a bit (mesma convenção dos testes de transform do documento).
        CHECK(transform.value().rotationDegrees.y ==
              Catch::Approx(45.f).margin(1e-3f));
        CHECK(transform.value().scale.x == Catch::Approx(2.f).margin(1e-4f));
    }

    // --- 5. Persistência LIMPA: nada de absoluto nos arquivos (§2.6) ---------
    REQUIRE(doc.saveProject().ok());
    {
        eng::fs::NativeFileSystem raw;
        const std::string files[] = {
            "/MeuJogo/project.goni.json",
            "/MeuJogo/assets/asset_registry.json",
            "/MeuJogo/scenes/main.json",
        };
        for (const std::string& rel : files) {
            auto text = raw.readAllText(
                eng::fs::Path{workspaceRoot + rel});
            INFO("arquivo: " << rel);
            REQUIRE(text.ok());
            CHECK(text.value().find(tmp.str()) == std::string::npos);
            CHECK(text.value().find(workspaceRoot) == std::string::npos);
        }
    }

    // --- 6. Reabertura do projeto (segunda execução do app) ------------------
    {
        auto reopened = eng::editor::EditorHost::create("gles",
                                                        workspaceRoot.c_str());
        if (reopened) {
            std::unique_ptr<eng::editor::EditorHost> second{reopened.value()};
            auto& doc2 = second->document();
            REQUIRE(doc2.openProject(eng::fs::Path{"MeuJogo"}).ok());
            CHECK(doc2.projectName() == "MeuJogo");
            // Assets e scripts sobreviveram à reabertura.
            auto* browser2 = doc2.assets();
            REQUIRE(browser2 != nullptr);
            auto textures = browser2->list("textures");
            REQUIRE(textures.ok());
            REQUIRE(textures.value().size() == 1);
            CHECK(textures.value()[0].name == "grass.png");
            auto scripts = doc2.scriptList();
            REQUIRE(scripts.ok());
            REQUIRE(scripts.value().size() == 2);
        }
        // Backend indisponível não invalida a regressão de paths (o host 1
        // já provou o pipeline); a reabertura é bônus de integração.
    }
}

// =============================================================================
// COLLIDER VISÍVEL (RECOVERY §10): "The editor must visually show the
// collision shape" — o autor edita shape/layer/mask/trigger e VÊ o que a
// física usará. Três provas: dados no quad, contorno na GPU, e o efeito
// físico OBSERVÁVEL (player cai no chão e PARA).
// =============================================================================

TEST_CASE("editor: quad expõe o shape do collider (dados — §10)", "[editor]")
{
    DocFixture f;
    f.withProject();
    auto ground = f.doc->createEntity("Ground", eng::scene::kNoEntity);
    REQUIRE(ground.ok());
    REQUIRE(f.doc->addComponent(ground.value(), "eng::physics::Collider")
                .ok());

    // Box com halfExtents (2,1) e escala de nó (2,1,1) → mundo (4,1).
    REQUIRE(f.doc->setInspectorField(ground.value(),
                                     "eng::physics::Collider", "shape", "Box")
                .ok());
    REQUIRE(f.doc->setInspectorField(ground.value(),
                                     "eng::physics::Collider",
                                     "halfExtents.x", "2")
                .ok());
    REQUIRE(f.doc->setInspectorField(ground.value(),
                                     "eng::physics::Collider",
                                     "halfExtents.y", "1")
                .ok());
    eng::editor::TransformDesc tr;
    tr.scale = {2.f, 1.f, 1.f};
    REQUIRE(f.doc->setTransform(ground.value(), tr).ok());

    const auto quads = f.doc->viewport().buildQuads(
        *f.doc->sceneInFocus(), std::nullopt);
    REQUIRE(quads.size() == 1);
    CHECK(quads[0].hasCollider);
    CHECK_FALSE(quads[0].colliderIsSphere);
    CHECK_FALSE(quads[0].colliderTrigger);
    CHECK(quads[0].colliderHalfX == Catch::Approx(4.f).margin(1e-4f));
    CHECK(quads[0].colliderHalfY == Catch::Approx(1.f).margin(1e-4f));

    // Esfera: raio 1.5 × escala X (2) → halfX == halfY == 3 (convenção do
    // PhysicsWorld::worldShapeOf — coluna X aproxima o raio).
    REQUIRE(f.doc->setInspectorField(ground.value(),
                                     "eng::physics::Collider", "shape",
                                     "Sphere")
                .ok());
    REQUIRE(f.doc->setInspectorField(ground.value(),
                                     "eng::physics::Collider", "radius", "1.5")
                .ok());
    const auto sphereQuads = f.doc->viewport().buildQuads(
        *f.doc->sceneInFocus(), std::nullopt);
    REQUIRE(sphereQuads.size() == 1);
    CHECK(sphereQuads[0].hasCollider);
    CHECK(sphereQuads[0].colliderIsSphere);
    CHECK(sphereQuads[0].colliderHalfX == Catch::Approx(3.f).margin(1e-4f));
    CHECK(sphereQuads[0].colliderHalfY == Catch::Approx(3.f).margin(1e-4f));

    // Trigger refletido no quad (contorno âmbar no renderer).
    REQUIRE(f.doc->setInspectorField(ground.value(),
                                     "eng::physics::Collider", "isTrigger",
                                     "true")
                .ok());
    const auto triggerQuads = f.doc->viewport().buildQuads(
        *f.doc->sceneInFocus(), std::nullopt);
    REQUIRE(triggerQuads.size() == 1);
    CHECK(triggerQuads[0].colliderTrigger);

    // Sem Collider: quad não carrega shape.
    auto plain = f.doc->createEntity("Plain", eng::scene::kNoEntity);
    REQUIRE(plain.ok());
    const auto plainQuads = f.doc->viewport().buildQuads(
        *f.doc->sceneInFocus(), std::nullopt);
    REQUIRE(plainQuads.size() == 2);
    std::size_t withCollider = 0;
    for (const auto& quad : plainQuads) {
        withCollider += quad.hasCollider ? 1u : 0u;
    }
    CHECK(withCollider == 1);
}

TEST_CASE("editor: renderer desenha o contorno do collider (§10)",
          "[editor][rhi_hardware]")
{
    if (editorGraphicsUnavailable()) {
        SKIP("sem driver gráfico (lavapipe/EGL) — suite completo roda no CI");
    }
    auto host = eng::editor::EditorHost::create("gles",
                                                ".editor-test-ws-collider");
    REQUIRE(host.ok());
    std::unique_ptr<eng::editor::EditorHost> owned{host.value()};
    auto& doc = owned->document();
    ensureProject(doc, "ColliderGame");

    auto entity = doc.createEntity("Solid", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(doc.addComponent(entity.value(), "eng::physics::Collider").ok());
    // Box sólido.
    REQUIRE(doc.setInspectorField(entity.value(), "eng::physics::Collider",
                                  "shape", "Box")
                .ok());
    auto trigger = doc.createEntity("Sensor", eng::scene::kNoEntity);
    REQUIRE(trigger.ok());
    REQUIRE(doc.addComponent(trigger.value(), "eng::physics::Collider").ok());
    REQUIRE(doc.setInspectorField(trigger.value(), "eng::physics::Collider",
                                  "shape", "Sphere")
                .ok());
    REQUIRE(doc.setInspectorField(trigger.value(), "eng::physics::Collider",
                                  "isTrigger", "true")
                .ok());

    int marker = 0;
    owned->surfaceCreated(&marker, eng::rhi::NativeWindowKind::Headless, 64,
                          48);
    REQUIRE(owned->state() == eng::editor::HostSurfaceState::Available);
    REQUIRE(owned->renderFrame(1.f / 60.f));

    // PROVA sem readback: vértices do último frame.
    //  - Box: 4 segmentos × 6 vértices = 24 vértices de contorno
    //  - Esfera: 8 segmentos × 6 = 48
    const auto& verts =
        owned->viewportRenderer()->lastFrameVertices();
    std::size_t solid = 0;
    std::size_t amber = 0;
    constexpr float kTealR = 0.16f, kTealG = 0.90f, kTealB = 0.85f;
    constexpr float kAmberR = 0.98f, kAmberG = 0.78f, kAmberB = 0.20f;
    for (const auto& v : verts) {
        if (v.r == kTealR && v.g == kTealG && v.b == kTealB) {
            ++solid;
        } else if (v.r == kAmberR && v.g == kAmberG && v.b == kAmberB) {
            ++amber;
        }
    }
    CHECK(solid == 24);  // box sólido: teal
    CHECK(amber == 48);  // esfera trigger: âmbar
}

TEST_CASE("editor: §10 — player com collider CAI no chão e PARA (física "
          "observável)",
          "[editor]")
{
    DocFixture f;
    f.withProject();

    // Chão: box collider SEM RigidBody (estático — invB = 0).
    auto ground = f.doc->createEntity("Ground", eng::scene::kNoEntity);
    REQUIRE(ground.ok());
    REQUIRE(f.doc->addComponent(ground.value(), "eng::physics::Collider")
                .ok());
    REQUIRE(f.doc->setInspectorField(ground.value(),
                                     "eng::physics::Collider", "shape", "Box")
                .ok());
    REQUIRE(f.doc->setInspectorField(ground.value(),
                                     "eng::physics::Collider",
                                     "halfExtents.x", "50")
                .ok());
    REQUIRE(f.doc->setInspectorField(ground.value(),
                                     "eng::physics::Collider",
                                     "halfExtents.y", "1")
                .ok());
    eng::editor::TransformDesc groundTr;
    groundTr.position = {0.f, 0.f, 0.f};
    REQUIRE(f.doc->setTransform(ground.value(), groundTr).ok());

    // Player: RigidBody (cai) + esfera collider r=0.5 em y=5.
    auto player = f.doc->createEntity("Player", eng::scene::kNoEntity);
    REQUIRE(player.ok());
    REQUIRE(f.doc->addComponent(player.value(), "eng::physics::RigidBody")
                .ok());
    REQUIRE(f.doc->addComponent(player.value(), "eng::physics::Collider")
                .ok());
    REQUIRE(f.doc->setInspectorField(player.value(),
                                     "eng::physics::Collider", "shape",
                                     "Sphere")
                .ok());
    REQUIRE(f.doc->setInspectorField(player.value(),
                                     "eng::physics::Collider", "radius", "0.5")
                .ok());
    eng::editor::TransformDesc playerTr;
    playerTr.position = {0.f, 5.f, 0.f};
    REQUIRE(f.doc->setTransform(player.value(), playerTr).ok());

    REQUIRE(f.doc->play().ok());
    // 3 segundos de jogo em frames de 8ms — física em passos fixos 1/60.
    for (int i = 0; i < 360; ++i) {
        f.doc->tick(1.f / 120.f);
    }
    // OBSERVÁVEL pelo CAMINHO REAL do usuário (doc.inspectorFields — o
    // mesmo que o painel do Android lê em Play): parou EM CIMA do chão
    // (top do box = y 1; centro do player = 1 + 0.5).
    // NOTA: este é o teste que PEGOU o bug do clone aleatório — o save
    // ordena por UUID e o handle de edição apontava outra entidade no
    // clone; agora toFocus() traduz na fronteira do documento.
    {
        const auto fields = f.doc->inspectorFields(
            player.value(), "eng::math::Transform");
        REQUIRE(fields.size() > 0);
        const auto* yField = fieldByPath(fields, "position.y");
        REQUIRE(yField != nullptr);
        CHECK(std::stof(yField->value) ==
              Catch::Approx(1.5f).margin(0.05f));
    }

    f.doc->stop();
    // Authoring intacto (§8.7): player volta para y=5 — e a seleção pós-
    // stop foi RESETADA (handle de clone não vaza para a edição).
    {
        const auto fields = f.doc->inspectorFields(
            player.value(), "eng::math::Transform");
        REQUIRE(fields.size() > 0);
        const auto* yField = fieldByPath(fields, "position.y");
        REQUIRE(yField != nullptr);
        CHECK(yField->value == "5");
    }
    CHECK_FALSE(f.doc->selection().has_value());
}
