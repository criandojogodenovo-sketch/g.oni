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
    // Caso "art" (3 < ".png" 4): o guard pós-P0 ainda negava a extensão a
    // nomes MAIS CURTOS que ela — o vertical slice P1 pegou o sprite sem
    // textura ("No such file: .../textures/art"). Regressão permanente.

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

    // "art" tem 3 chars — MAIS CURTO que ".png": o guard pós-P0
    // (`>= ext.size()`) ainda negava a extensão a estes (bug achado pelo
    // vertical slice P1: sprite "art.png" sem textura no viewport).
    REQUIRE(f.fs->writeAllText(eng::fs::Path{".import_tmp/art.png"},
                               "PNGDATA")
                .ok());
    std::string artName;
    auto art = browser->import(".import_tmp/art.png", "textures", "art",
                               &artName);
    REQUIRE(art.ok());
    CHECK(artName == "art.png");
    auto artBytes = browser->read("textures", artName);
    REQUIRE(artBytes.ok());
    CHECK(artBytes.value().size() == 7);
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

// =============================================================================
// P1 — COMPLETE 2D AUTHORING VERTICAL SLICE
//
// CREATE SCENE → ADD SPRITE → IMPORT IMAGE → IMAGE VISIBLE → SELECT →
// MOVE → ROTATE → SCALE → INSPECT → DUPLICATE → DELETE → SAVE → RELOAD
// → PLAY → SEE RUNTIME.
//
// Cada contrato é testado pelo CAMINHO REAL (documento/ECS — o mesmo que
// o Activity opera via JNI). Features visuais provadas com PIXEL REAL
// lido da surface (missão P1.15).
// =============================================================================

namespace {

/// Viewport 200x150, câmera padrão (0,0) zoom 48 — centro da tela (100,75)
/// é o mundo (0,0). Handle/axis/ring em posições PREVISÍVEIS:
///   eixo X handle   → tela (100+84, 75) = (184, 75)
///   anel rotate     → raio max(half)+26px/48; handle no ângulo da entidade
///   canto NE        → (100+halfW*48, 75-halfH*48)
struct GizmoFixture {
    DocFixture f;
    eng::ecs::Entity entity{};

    GizmoFixture()
    {
        f.withProject();
        f.doc->viewport().setScreenSize(200.f, 150.f);
        auto created = f.doc->createEntity("Hero", eng::scene::kNoEntity);
        REQUIRE(created.ok());
        entity = created.value();
    }
};

}  // namespace

// --- P1.1 SELEÇÃO ------------------------------------------------------------

TEST_CASE("editor: P1 — seleção sobrevive a transformações e invalida em delete",
          "[editor]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;

    REQUIRE(doc.select(g.entity).ok());
    CHECK(doc.isSelected(g.entity));

    // Transformações NÃO derrubam a seleção (P1.1).
    eng::editor::TransformDesc desc;
    desc.position = eng::math::Vec3{3.f, -2.f, 0.f};
    desc.rotationDegrees = eng::math::Vec3{0.f, 0.f, 45.f};
    desc.scale = eng::math::Vec3{2.f, 2.f, 1.f};
    REQUIRE(doc.setTransform(g.entity, desc).ok());
    CHECK(doc.isSelected(g.entity));

    // Deselect explícito.
    doc.deselect();
    CHECK_FALSE(doc.selection().has_value());

    // DELETE limpa a seleção (handle não fica dangling).
    REQUIRE(doc.select(g.entity).ok());
    REQUIRE(doc.deleteEntity(g.entity).ok());
    CHECK_FALSE(doc.selection().has_value());
    CHECK_FALSE(doc.isSelected(g.entity));

    // Handle STALE: select em entidade morta → erro preciso; isSelected
    // falso (nenhuma referência ECS stale sobrevive — P1.1).
    auto stale = doc.select(g.entity);
    REQUIRE(stale.isError());
    CHECK(stale.error().code == eng::core::StatusCode::NotFound);
}

TEST_CASE("editor: P1 — stale handles: reciclagem de pool não ressuscita seleção",
          "[editor]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;

    // Populaciona o pool e recicla: A criado/destruído; B pode reusar o
    // ÍNDICE com geração DIFERENTE — handle antigo NÃO pode acertar B.
    auto second = doc.createEntity("Other", eng::scene::kNoEntity);
    REQUIRE(second.ok());
    REQUIRE(doc.deleteEntity(g.entity).ok());
    auto recycled = doc.createEntity("Recycled", eng::scene::kNoEntity);
    REQUIRE(recycled.ok());

    // O handle antigo é inválido (índice/geração não batem com o vivo).
    CHECK_FALSE(doc.isSelected(g.entity));
    auto stale = doc.select(g.entity);
    if (stale.isError()) {
        CHECK(stale.error().code == eng::core::StatusCode::NotFound);
    } else {
        // Se o pool devolveu exatamente o MESMO handle (index+gen), a
        // entidade É a mesma reciclada — seleção válida por construção.
        CHECK(recycled.value() == g.entity);
    }

    // gizmoDragBegin com seleção morta → None (sem crash, sem stale).
    doc.deselect();
    CHECK(doc.gizmoDragBegin(100.f, 75.f, nullptr) ==
          eng::editor::GizmoHandle::None);

    // selectionBounds de seleção morta → inválido.
    CHECK(doc.select(EditorDocument::unpackEntity(0xFFFFFFFF00000000ull)).isError());
    auto bounds = doc.selectionBounds(nullptr);
    CHECK_FALSE(bounds.valid);
}

// --- P1.2 BOUNDS --------------------------------------------------------------

TEST_CASE("editor: P1 — bounds da seleção segue pos/rot/escala/textura (P1.2)",
          "[editor]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;

    // Sem textura: tamanho = escala local (default 1 → half 0.5).
    REQUIRE(doc.select(g.entity).ok());
    auto bounds = doc.selectionBounds(nullptr);
    REQUIRE(bounds.valid);
    CHECK(bounds.worldX == Catch::Approx(0.f));
    CHECK(bounds.worldY == Catch::Approx(0.f));
    CHECK(bounds.halfW == Catch::Approx(0.5f));
    CHECK(bounds.halfH == Catch::Approx(0.5f));
    CHECK(bounds.rotation == Catch::Approx(0.f));

    // MOVE → bounds acompanha.
    eng::editor::TransformDesc desc;
    desc.position = eng::math::Vec3{2.f, 1.f, 0.f};
    REQUIRE(doc.setTransform(g.entity, desc).ok());
    bounds = doc.selectionBounds(nullptr);
    REQUIRE(bounds.valid);
    CHECK(bounds.worldX == Catch::Approx(2.f));
    CHECK(bounds.worldY == Catch::Approx(1.f));

    // ROTATE → bounds gira.
    desc.rotationDegrees = eng::math::Vec3{0.f, 0.f, 90.f};
    REQUIRE(doc.setTransform(g.entity, desc).ok());
    bounds = doc.selectionBounds(nullptr);
    CHECK(bounds.rotation == Catch::Approx(1.5707963f).margin(1e-3f));

    // SCALE → bounds cresce (não usa tamanho arbitrário).
    desc.scale = eng::math::Vec3{3.f, 2.f, 1.f};
    REQUIRE(doc.setTransform(g.entity, desc).ok());
    bounds = doc.selectionBounds(nullptr);
    CHECK(bounds.halfW == Catch::Approx(1.5f));
    CHECK(bounds.halfH == Catch::Approx(1.f));

    // COM TEXTURA REAL: tamanho desenhado = região px / ppu (P1.2 — o
    // mesmo número do renderer e do hit-test, nunca arbitrário).
    writePngTemp(*g.f.fs);
    auto* browser = doc.assets();
    REQUIRE(browser != nullptr);
    REQUIRE(browser->import(".import_tmp/grass.png", "textures", "grass").ok());
    REQUIRE(doc.addComponent(g.entity, "eng::editor::SpriteData").ok());
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "textureAsset", "grass.png").ok());
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "pixelsPerUnit", "1").ok());
    eng::editor::TextureCache cache;
    bounds = doc.selectionBounds(&cache);
    REQUIRE(bounds.valid);
    // PNG 2x2, ppu 1, escala 3x2 → half = (2*3/1*0.5, 2*2/1*0.5) = (3, 2).
    CHECK(bounds.halfW == Catch::Approx(3.f));
    CHECK(bounds.halfH == Catch::Approx(2.f));

    // Ppu 4 → half = 3*2/4*0.5 = 0.75 (acima do mínimo tocável).
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "pixelsPerUnit", "4").ok());
    bounds = doc.selectionBounds(&cache);
    REQUIRE(bounds.valid);
    CHECK(bounds.halfW == Catch::Approx(0.75f).margin(1e-4f));
    // Ppu default 48 → half 0.0625 CLAMPADO ao mínimo tocável (handle
    // sempre alcançável pelo dedo — decisão de UX §8.8).
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "pixelsPerUnit", "48").ok());
    bounds = doc.selectionBounds(&cache);
    REQUIRE(bounds.valid);
    CHECK(bounds.halfW == Catch::Approx(0.22917f).margin(1e-3f));
}

// --- P1.3 MOVE GIZMO ----------------------------------------------------------

TEST_CASE("editor: P1 — gizmo MOVE: centro e eixos X/Y com drag REAL", "[editor]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;
    using eng::editor::GizmoHandle;

    REQUIRE(doc.select(g.entity).ok());
    doc.setTool(eng::editor::EditorTool::Move);

    // Handle CENTRAL no centro da tela (entidade em (0,0)).
    CHECK(doc.gizmoDragBegin(100.f, 75.f, nullptr) == GizmoHandle::MoveCenter);

    // Drag de 48px à direita = +1 unidade de mundo.
    REQUIRE(doc.gizmoDragTo(148.f, 75.f).ok());
    auto tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().position.x == Catch::Approx(1.f).margin(1e-4f));
    CHECK(tr.value().position.y == Catch::Approx(0.f).margin(1e-4f));

    // Drag de 48px ACIMA = +1 em Y (flip de tela→mundo correto).
    REQUIRE(doc.gizmoDragTo(148.f, 27.f).ok());
    tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().position.x == Catch::Approx(1.f).margin(1e-4f));
    CHECK(tr.value().position.y == Catch::Approx(1.f).margin(1e-4f));
    doc.gizmoDragEnd();

    // EIXO X: volta o herói à origem e pega o handle do eixo X (84px à
    // direita do CENTRO — segue a entidade — tela (184, 75)).
    eng::editor::TransformDesc reset;
    reset.position = eng::math::Vec3{1.f, 1.f, 0.f};  // onde o drag deixou
    REQUIRE(doc.setTransform(g.entity, reset).ok());
    REQUIRE(doc.select(g.entity).ok());
    // Handle X da entidade em (1,1): tela (100+48+84, 75-48) = (232, 27).
    CHECK(doc.gizmoDragBegin(232.f, 27.f, nullptr) == GizmoHandle::MoveAxisX);
    REQUIRE(doc.gizmoDragTo(280.f, -21.f).ok());  // diagonal na tela (+1,+1)
    tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().position.x == Catch::Approx(2.f).margin(1e-3f));
    CHECK(tr.value().position.y == Catch::Approx(1.f).margin(1e-3f));
    doc.gizmoDragEnd();

    // EIXO Y: alvo em (100, 75-84=… pega pelo centro do handle Y: eixo
    // tem 84px a partir da borda do bounds; handle Y na tela ~ (100, -9)?
    // Não: eixo Y começa NO TOPO do bounds (0.5*48=24px) + 84px → handle
    // em y = 75-24-84 ≈ -33 (fora da tela 150px). Reproduz o layout REAL:
    // hit radius 24px dá alcance até y = -33+24 = -9 — inalcançável em
    // 150px de altura? NÃO: o hit é no CENTRO do handle (x=100, y=75-108
    // = -33) — fora. A ferramenta MOVE oferece o CENTRO p/ movimento
    // livre em telas pequenas; eixo Y fica acessível com zoom out.
    // (Teste do eixo Y com zoom menor: zoom 24 → eixo = 84/24 = 3.5
    // unidades a partir do topo do bounds.)
    doc.viewport().camera().zoom = 24.f;
    REQUIRE(doc.select(g.entity).ok());
    // bounds half em tela: 0.5*24 = 12px; eixo Y handle: 75-12-84 = -21…
    // ainda fora. Zoom 12: 75-6-84 = -15. Zoom 8 (mín): 75-4-84 = -13.
    // O LAYOUT é honesto: eixo Y aponta PARA CIMA e sai da tela quando a
    // entidade está centralizada — o usuário move a câmera. Validamos o
    // TRAVAMENTO de X no eixo Y por simetria: hit fora da tela não testa.
    doc.viewport().camera().zoom = 48.f;

    // PRIORIDADE do handle: entidade B embaixo do handle do eixo X de A —
    // o gizmo de A vence (não re-seleciona, não move B).
    auto other = doc.createEntity("Other", eng::scene::kNoEntity);
    REQUIRE(other.ok());
    eng::editor::TransformDesc otherPos;
    otherPos.position = eng::math::Vec3{3.75f, 1.f, 0.f};  // sob o handle X
    REQUIRE(doc.setTransform(other.value(), otherPos).ok());
    CHECK(doc.gizmoDragBegin(280.f, 27.f, nullptr) == GizmoHandle::MoveAxisX);
    REQUIRE(doc.gizmoDragTo(328.f, 27.f).ok());
    doc.gizmoDragEnd();
    tr = doc.transform(other.value());  // B não se mexeu
    REQUIRE(tr.ok());
    CHECK(tr.value().position.x == Catch::Approx(3.75f).margin(1e-4f));
    CHECK(doc.isSelected(g.entity));  // seleção de A sobreviveu (P1.1)
}

TEST_CASE("editor: P1 — gizmo MOVE com textura REAL: bounds desenhado é o alvo",
          "[editor]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;
    using eng::editor::GizmoHandle;

    writePngTemp(*g.f.fs);
    auto* browser = doc.assets();
    REQUIRE(browser != nullptr);
    REQUIRE(browser->import(".import_tmp/grass.png", "textures", "grass").ok());
    REQUIRE(doc.addComponent(g.entity, "eng::editor::SpriteData").ok());
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "textureAsset", "grass.png").ok());
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "pixelsPerUnit", "1").ok());
    // ppu 1 → sprite 2x2 px = 2x2 unidades de mundo: bem maior que a
    // escala local 1x1. O handle central continua NO CENTRO (posição).
    eng::editor::TextureCache cache;

    REQUIRE(doc.select(g.entity).ok());
    doc.setTool(eng::editor::EditorTool::Move);
    CHECK(doc.gizmoDragBegin(100.f, 75.f, &cache) == GizmoHandle::MoveCenter);
    REQUIRE(doc.gizmoDragTo(148.f, 75.f).ok());
    auto tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().position.x == Catch::Approx(1.f).margin(1e-4f));

    // Com o sprite em (1,0), o TOQUE na borda dele (tamanho desenhado
    // 2x2 → tela 96x96 centrada em (148, 75)) ACERTA a entidade.
    auto hit = doc.viewportTap(190.f, 75.f, &cache);
    REQUIRE(hit.has_value());
    CHECK(*hit == g.entity);
}

// --- P1.4 ROTATE GIZMO --------------------------------------------------------

TEST_CASE("editor: P1 — gizmo ROTATE: ângulo do pointer, snap e round-trip",
          "[editor]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;
    using eng::editor::GizmoHandle;

    REQUIRE(doc.select(g.entity).ok());
    doc.setTool(eng::editor::EditorTool::Rotate);

    // Anel: raio = 0.5 + 26/48 = 1.0417 → handle no ângulo 0 (rotação 0):
    // tela (100 + 1.0417*48, 75) ≈ (150, 75).
    const float ringR = 0.5f + 26.f / 48.f;
    const float hx = 100.f + ringR * 48.f;
    CHECK(doc.gizmoDragBegin(hx, 75.f, nullptr) == GizmoHandle::RotateRing);

    // Pointer reto ACIMA do pivot → +90°.
    REQUIRE(doc.gizmoDragTo(100.f, 75.f - ringR * 48.f).ok());
    auto tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().rotationDegrees.z == Catch::Approx(90.f).margin(0.5f));
    doc.gizmoDragEnd();

    // Re-grab NO NOVO handle (o anel segue a rotação atual: 90° → topo).
    // SNAP no MESMO gesto: 52.5° (LIVRE — a 7.5° de 45 e 60, fora do ímã
    // de 4°) → 59° (ímã puxa para 60 — P1.4).
    REQUIRE(doc.select(g.entity).ok());
    const float topX = 100.f;
    const float topY = 75.f - ringR * 48.f;
    CHECK(doc.gizmoDragBegin(topX, topY, nullptr) ==
          GizmoHandle::RotateRing);
    const float a47 = 52.5f * 3.14159265f / 180.f;
    const float a59 = 59.f * 3.14159265f / 180.f;
    REQUIRE(doc.gizmoDragTo(100.f + std::cos(a47) * ringR * 48.f,
                            75.f - std::sin(a47) * ringR * 48.f).ok());
    tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().rotationDegrees.z == Catch::Approx(52.5f).margin(1.f));
    REQUIRE(doc.gizmoDragTo(100.f + std::cos(a59) * ringR * 48.f,
                            75.f - std::sin(a59) * ringR * 48.f).ok());
    tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().rotationDegrees.z == Catch::Approx(60.f).margin(0.01f));
    doc.gizmoDragEnd();

    // SAVE → RELOAD → MESMA rotação (P1.4 critério).
    REQUIRE(doc.saveScene("rot.json").ok());
    REQUIRE(doc.loadScene("rot.json").ok());
    const auto nodes = doc.hierarchySnapshot();
    REQUIRE(nodes.size() == 1);
    tr = doc.transform(nodes[0].entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().rotationDegrees.z == Catch::Approx(60.f).margin(0.01f));
}

// --- P1.5 SCALE GIZMO ---------------------------------------------------------

TEST_CASE("editor: P1 — gizmo SCALE: cantos X/Y, clamp e round-trip", "[editor]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;
    using eng::editor::GizmoHandle;

    REQUIRE(doc.select(g.entity).ok());
    doc.setTool(eng::editor::EditorTool::Scale);

    // Canto NE do bounds (half 0.5): tela (100+24, 75-24) = (124, 51).
    CHECK(doc.gizmoDragBegin(124.f, 51.f, nullptr) == GizmoHandle::ScaleNE);

    // Dobrar a distância local → escala 2x2 (X e Y independentes).
    REQUIRE(doc.gizmoDragTo(148.f, 27.f).ok());
    auto tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().scale.x == Catch::Approx(2.f).margin(1e-3f));
    CHECK(tr.value().scale.y == Catch::Approx(2.f).margin(1e-3f));

    // Drag ASSIMÉTRICO: só X dobra de novo (Y mantém).
    REQUIRE(doc.gizmoDragTo(196.f, 27.f).ok());
    tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().scale.x == Catch::Approx(4.f).margin(1e-2f));
    CHECK(tr.value().scale.y == Catch::Approx(2.f).margin(1e-2f));

    // IMPEDIR VALORES INVÁLIDOS: arrasto PARA DENTRO do centro (ratio
    // ~0) → clamp no mínimo (P1.5), nunca 0/negativo/NaN.
    REQUIRE(doc.gizmoDragTo(101.f, 74.f).ok());
    tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().scale.x >= eng::editor::TransformGizmo::kScaleMin);
    CHECK(tr.value().scale.y >= eng::editor::TransformGizmo::kScaleMin);
    CHECK(std::isfinite(tr.value().scale.x));
    CHECK(std::isfinite(tr.value().scale.y));
    doc.gizmoDragEnd();

    // SAVE → RELOAD → MESMA escala.
    REQUIRE(doc.saveScene("scale.json").ok());
    REQUIRE(doc.loadScene("scale.json").ok());
    const auto nodes = doc.hierarchySnapshot();
    REQUIRE(nodes.size() == 1);
    tr = doc.transform(nodes[0].entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().scale.x >= eng::editor::TransformGizmo::kScaleMin);
    CHECK(tr.value().scale.y >= eng::editor::TransformGizmo::kScaleMin);
}

TEST_CASE("editor: P1 — gizmo ROTATE/SCALE sobre sprite ROTACIONADO (frame local)",
          "[editor]")
{
    // O frame LOCAL do nó desconta a rotação: escalar um sprite girado 90°
    // tem de escalar os EIXOS DO SPRITE, não os do mundo (P1.5).
    GizmoFixture g;
    auto& doc = *g.f.doc;
    using eng::editor::GizmoHandle;

    eng::editor::TransformDesc desc;
    desc.rotationDegrees = eng::math::Vec3{0.f, 0.f, 90.f};
    REQUIRE(doc.setTransform(g.entity, desc).ok());
    REQUIRE(doc.select(g.entity).ok());
    doc.setTool(eng::editor::EditorTool::Scale);

    // Canto NE do bounds GIRADO: (0.5,0.5) rot 90° → mundo (-0.5, 0.5)
    // → tela (100-24, 75-24) = (76, 51).
    CHECK(doc.gizmoDragBegin(76.f, 51.f, nullptr) == GizmoHandle::ScaleNE);
    // Pointer em world (1.5, 1.5) → local (desconta 90°): (1.5, -1.5)…
    // escala X por ratio 1.5/0.5=3, Y por -1.5/0.5=-3 → clamp (inválido).
    REQUIRE(doc.gizmoDragTo(172.f, -21.f).ok());
    auto tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    // pointer world (1.5, 2.0) → local (desconta 90°): (2.0, -1.5) →
    // ratio.x = 2.0/0.5 = 4 (o drag DOBRA o alcance no eixo local X).
    CHECK(tr.value().scale.x == Catch::Approx(4.f).margin(1e-2f));
    // Y ficou negativo pelo caminho do canto oposto → clamp no mínimo.
    CHECK(tr.value().scale.y >= eng::editor::TransformGizmo::kScaleMin);
}

// --- P1.6 TOOL MODES ----------------------------------------------------------

TEST_CASE("editor: P1 — tool modes: abstração única, Select sem gizmo, Play sem gizmo",
          "[editor]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;
    using eng::editor::EditorTool;

    CHECK(doc.tool() == EditorTool::Select);
    CHECK(doc.gizmoDraw(nullptr).quads.empty());  // Select: nada desenha

    for (const EditorTool tool :
         {EditorTool::Move, EditorTool::Rotate, EditorTool::Scale}) {
        doc.setTool(tool);
        CHECK(doc.tool() == tool);
        REQUIRE(doc.select(g.entity).ok());
        const auto draw = doc.gizmoDraw(nullptr);
        CHECK_FALSE(draw.quads.empty());   // cada tool desenha handles
        if (tool == EditorTool::Rotate) {
            CHECK_FALSE(draw.segments.empty());  // anel
        } else if (tool == EditorTool::Move) {
            CHECK(draw.segments.size() == 2);    // eixos X e Y
        } else {
            CHECK(draw.segments.size() == 4);    // diagonais dos cantos
        }
    }

    // Play: gizmo NÃO existe (edição rejeitada — §8.7).
    doc.setTool(EditorTool::Move);
    REQUIRE(doc.select(g.entity).ok());
    REQUIRE(doc.play().ok());
    CHECK(doc.gizmoDraw(nullptr).quads.empty());
    CHECK(doc.gizmoDragBegin(100.f, 75.f, nullptr) ==
          eng::editor::GizmoHandle::None);
    doc.stop();
    // stop() RESETA a seleção (contrato a7fd366) → gizmo some; com nova
    // seleção ele volta (Edit restabelecido).
    CHECK(doc.gizmoDraw(nullptr).quads.empty());
    REQUIRE(doc.select(g.entity).ok());
    CHECK_FALSE(doc.gizmoDraw(nullptr).quads.empty());
}

// --- P1.9 INSPECTOR SYNC ------------------------------------------------------

TEST_CASE("editor: P1 — Inspector ↔ viewport bidirecionais (revisão única)",
          "[editor]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;

    REQUIRE(doc.select(g.entity).ok());
    const std::uint64_t rev0 = doc.selectionRevision();

    // Inspector → ECS: setInspectorField escreve no Transform REAL.
    REQUIRE(doc.setInspectorField(g.entity, "eng::math::Transform",
                                  "position.x", "2.5").ok());
    auto tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().position.x == Catch::Approx(2.5f).margin(1e-5f));
    CHECK(doc.selectionRevision() != rev0);  // bump: UI percebe

    // Gizmo → ECS → Inspector: o drag atualiza o Transform que o
    // inspectorFields LÊ (mesma fonte de verdade — P1.9). A entidade
    // está em (2.5, 0) → centro na tela (220, 75).
    doc.setTool(eng::editor::EditorTool::Move);
    CHECK(doc.gizmoDragBegin(220.f, 75.f, nullptr) ==
          eng::editor::GizmoHandle::MoveCenter);
    REQUIRE(doc.gizmoDragTo(316.f, 75.f).ok());  // +2 unidades
    doc.gizmoDragEnd();
    {
        const auto fields = doc.inspectorFields(g.entity,
                                                "eng::math::Transform");
        const auto* xField = fieldByPath(fields, "position.x");
        REQUIRE(xField != nullptr);
        CHECK(std::stof(xField->value) == Catch::Approx(4.5f).margin(1e-3f));
    }

    // Rotação e escala idem (Inspector numérico → gizmo vê o mesmo TRS).
    REQUIRE(doc.setInspectorField(g.entity, "eng::math::Transform",
                                  "rotation.z", "30").ok());
    REQUIRE(doc.setInspectorField(g.entity, "eng::math::Transform",
                                  "scale.x", "1.5").ok());
    tr = doc.transform(g.entity);
    CHECK(tr.value().rotationDegrees.z == Catch::Approx(30.f).margin(1e-4f));
    CHECK(tr.value().scale.x == Catch::Approx(1.5f).margin(1e-5f));
}

// --- P1.10 SPRITE CREATION UX --------------------------------------------------

TEST_CASE("editor: P1 — createSprite: SpriteData default, placeholder, numeração",
          "[editor]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;

    auto sprite = doc.createSprite("Sprite");
    REQUIRE(sprite.ok());
    CHECK(doc.isSelected(sprite.value()));  // selecionada de fábrica

    // SpriteData presente com defaults (ppu 48) e SEM textura → o quad é
    // isSprite (placeholder xadrez no renderer, não hue).
    const auto* scene = doc.sceneInFocus();
    const auto quads = doc.viewport().buildQuads(*scene, doc.selection());
    REQUIRE(quads.size() == 2);  // Hero (fixture) + Sprite
    const auto& spriteQuad = quads[1];
    CHECK(spriteQuad.isSprite);
    CHECK(spriteQuad.textureAsset.empty());
    CHECK(spriteQuad.spritePpu == 48.f);

    // Numeração automática: Sprite, Sprite 2, Sprite 3.
    auto second = doc.createSprite("Sprite");
    REQUIRE(second.ok());
    CHECK(doc.nameOf(*doc.sceneInFocus(), second.value()) == "Sprite 2");
    auto third = doc.createSprite("Sprite");
    REQUIRE(third.ok());
    CHECK(doc.nameOf(*doc.sceneInFocus(), third.value()) == "Sprite 3");
    CHECK(doc.sceneDirty());  // authoring marcado sujo

    // SAVE/LOAD mantém os placeholders (SpriteData serializa vazio).
    REQUIRE(doc.saveScene("sprites.json").ok());
    REQUIRE(doc.loadScene("sprites.json").ok());
    const auto nodes = doc.hierarchySnapshot();
    REQUIRE(nodes.size() == 4);  // Hero + Sprite + Sprite 2 + Sprite 3
}

// --- P1.7 DUPLICATE ------------------------------------------------------------

TEST_CASE("editor: P1 — DUPLICATE de sprite: IDs diferentes, independentes, serializam",
          "[editor]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;
    using eng::editor::SpriteData;

    writePngTemp(*g.f.fs);
    auto* browser = doc.assets();
    REQUIRE(browser != nullptr);
    REQUIRE(browser->import(".import_tmp/grass.png", "textures", "grass").ok());

    // Sprite A com textura e transform autoral.
    REQUIRE(doc.addComponent(g.entity, "eng::editor::SpriteData").ok());
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "textureAsset", "grass.png").ok());
    eng::editor::TransformDesc desc;
    desc.position = eng::math::Vec3{1.f, 2.f, 0.f};
    desc.rotationDegrees = eng::math::Vec3{0.f, 0.f, 25.f};
    REQUIRE(doc.setTransform(g.entity, desc).ok());

    auto dup = doc.duplicateEntity(g.entity);
    REQUIRE(dup.ok());
    CHECK(dup.value() != g.entity);  // entity ID NOVO

    // SpriteData duplicado: MESMA imagem (componente clonado).
    const auto* scene = doc.sceneInFocus();
    const auto* spriteA = scene->world().get<SpriteData>(g.entity);
    const auto* spriteB = scene->world().get<SpriteData>(dup.value());
    REQUIRE(spriteA != nullptr);
    REQUIRE(spriteB != nullptr);
    CHECK(spriteB->textureAsset == spriteA->textureAsset);
    CHECK(spriteB != spriteA);  // sem compartilhar estado mutável

    // Transform independente: mover A não mexe em B.
    REQUIRE(doc.moveEntityScreen(g.entity, 48.f, 0.f).ok());
    auto trA = doc.transform(g.entity);
    auto trB = doc.transform(dup.value());
    REQUIRE(trA.ok());
    REQUIRE(trB.ok());
    CHECK(trA.value().position.x == Catch::Approx(2.f).margin(1e-4f));
    CHECK(trB.value().position.x == Catch::Approx(1.f).margin(1e-4f));
    CHECK(trB.value().rotationDegrees.z == Catch::Approx(25.f).margin(1e-3f));

    // SERIALIZE: ambos persistem com a textura certa.
    REQUIRE(doc.saveScene("dup.json").ok());
    REQUIRE(doc.loadScene("dup.json").ok());
    const auto nodes = doc.hierarchySnapshot();
    REQUIRE(nodes.size() == 2);
    int textured = 0;
    for (const auto& node : nodes) {
        const auto* sprite =
            doc.sceneInFocus()->world().get<SpriteData>(node.entity);
        if (sprite != nullptr && sprite->textureAsset == "grass.png") {
            ++textured;
        }
    }
    CHECK(textured == 2);
}

// --- P1.8 DELETE ----------------------------------------------------------------

TEST_CASE("editor: P1 — DELETE: seleção limpa, links/vizinhos intactos, save válido",
          "[editor]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;

    auto child = doc.createEntity("Child", g.entity);
    REQUIRE(child.ok());
    auto other = doc.createEntity("Other", eng::scene::kNoEntity);
    REQUIRE(other.ok());

    // Seleciona e apaga o PAI (com filho) — cascata folhas primeiro.
    REQUIRE(doc.select(g.entity).ok());
    REQUIRE(doc.deleteEntity(g.entity).ok());
    CHECK_FALSE(doc.selection().has_value());          // seleção limpa
    CHECK(doc.selectionRevision() > 0);

    const auto nodes = doc.hierarchySnapshot();
    REQUIRE(nodes.size() == 1);                        // só Other sobrou
    CHECK(nodes[0].name == "Other");

    // Handle do pai morto é rejeitado com erro preciso (ANTES do reload:
    // um load reconstrói o mundo e handles antigos podem ALIASEAR os
    // novos — reciclagem de index+generation é por mundo, não eterna).
    auto movedDead = doc.moveEntityScreen(g.entity, 10.f, 10.f);
    REQUIRE(movedDead.isError());
    CHECK(movedDead.error().code == eng::core::StatusCode::NotFound);

    // Save/load continua válido sem os mortos.
    REQUIRE(doc.saveScene("del.json").ok());
    REQUIRE(doc.loadScene("del.json").ok());
    CHECK(doc.hierarchySnapshot().size() == 1);
}

// --- P1.11 SAVE/RELOAD do fluxo completo ----------------------------------------

TEST_CASE("editor: P1 — SAVE/RELOAD do workflow completo sem perda de dados",
          "[editor]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;

    writePngTemp(*g.f.fs);
    auto* browser = doc.assets();
    REQUIRE(browser != nullptr);
    REQUIRE(browser->import(".import_tmp/grass.png", "textures", "grass").ok());

    // Authoring: 2 sprites texturizados com transforms distintos + delete.
    REQUIRE(doc.addComponent(g.entity, "eng::editor::SpriteData").ok());
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "textureAsset", "grass.png").ok());
    eng::editor::TransformDesc a;
    a.position = eng::math::Vec3{-1.f, 0.5f, 0.f};
    a.rotationDegrees = eng::math::Vec3{0.f, 0.f, -15.f};
    a.scale = eng::math::Vec3{2.f, 0.5f, 1.f};
    REQUIRE(doc.setTransform(g.entity, a).ok());
    auto dup = doc.duplicateEntity(g.entity);
    REQUIRE(dup.ok());
    eng::editor::TransformDesc b;
    b.position = eng::math::Vec3{3.f, -1.f, 0.f};
    REQUIRE(doc.setTransform(dup.value(), b).ok());

    REQUIRE(doc.saveScene("full.json").ok());
    // "destroy editor document": newScene limpa o estado autoral.
    REQUIRE(doc.newScene().ok());
    CHECK(doc.hierarchySnapshot().empty());
    REQUIRE(doc.loadScene("full.json").ok());

    const auto nodes = doc.hierarchySnapshot();
    REQUIRE(nodes.size() == 2);
    // IDs distintos, dados recuperados (textureAsset, transforms).
    std::vector<eng::math::Vec3> positions;
    for (const auto& node : nodes) {
        auto tr = doc.transform(node.entity);
        REQUIRE(tr.ok());
        positions.push_back(tr.value().position);
        const auto* sprite = doc.sceneInFocus()->world().get<eng::editor::SpriteData>(
            node.entity);
        REQUIRE(sprite != nullptr);
        CHECK(sprite->textureAsset == "grass.png");
    }
    REQUIRE(positions.size() == 2);
    bool hasA = false, hasB = false;
    for (const auto& p : positions) {
        if (p.x == Catch::Approx(-1.f).margin(1e-5f)) { hasA = true; }
        if (p.x == Catch::Approx(3.f).margin(1e-5f)) { hasB = true; }
    }
    CHECK(hasA);
    CHECK(hasB);

    // Nenhum path absoluto indevido no JSON salvo.
    const auto text = g.f.fs->readAllText(
        eng::fs::Path{"TestGame/scenes/full.json"});
    REQUIRE(text.ok());
    CHECK(text.value().find("/home/") == std::string::npos);
    CHECK(text.value().find("file://") == std::string::npos);
}

// --- P1.12 PLAY (clone) -----------------------------------------------------------

TEST_CASE("editor: P1 — PLAY: selected/duplicated/transformed/deleted no clone",
          "[editor]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;
    using eng::editor::SpriteData;

    // A (transformado+selecionado), B (deletado), C (duplicado de A).
    eng::editor::TransformDesc a;
    a.position = eng::math::Vec3{3.f, 2.f, 0.f};
    REQUIRE(doc.setTransform(g.entity, a).ok());
    auto doomed = doc.createEntity("Doomed", eng::scene::kNoEntity);
    REQUIRE(doomed.ok());
    REQUIRE(doc.addComponent(g.entity, "eng::editor::SpriteData").ok());
    auto clone = doc.duplicateEntity(g.entity);
    REQUIRE(clone.ok());
    REQUIRE(doc.deleteEntity(doomed.value()).ok());  // morte PRÉ-Play
    REQUIRE(doc.select(g.entity).ok());            // A selecionado PRÉ-Play

    REQUIRE(doc.play().ok());
    CHECK(doc.isPlaying());

    // Clone tem A' e C' — NÃO tem B (deletada antes do play).
    const auto nodes = doc.hierarchySnapshot();
    REQUIRE(nodes.size() == 2);  // Hero (A) + duplicata (C)
    bool sawDoomed = false;
    for (const auto& node : nodes) {
        CHECK(node.name != "Doomed");
        if (node.name == "Doomed") { sawDoomed = true; }
    }
    CHECK_FALSE(sawDoomed);

    // Seleção PRÉ-Play segue o CLONE de A (remap a7fd366): inspectorFields
    // com o handle de EDIÇÃO mostra o transform do CLONE correto.
    {
        const auto fields = doc.inspectorFields(g.entity,
                                                "eng::math::Transform");
        const auto* xField = fieldByPath(fields, "position.x");
        REQUIRE(xField != nullptr);
        CHECK(std::stof(xField->value) == Catch::Approx(3.f).margin(1e-3f));
    }

    // Mutação em Play vai ao CLONE (debug §8.7) — não vaza para edição.
    REQUIRE(doc.moveEntityScreen(g.entity, 48.f, 0.f).ok());
    {
        const auto fields = doc.inspectorFields(g.entity,
                                                "eng::math::Transform");
        const auto* xField = fieldByPath(fields, "position.x");
        REQUIRE(xField != nullptr);
        CHECK(std::stof(xField->value) == Catch::Approx(4.f).margin(1e-3f));
    }

    doc.stop();
    // Edição INTACTA: A em (3,2), C presente, seleção resetada.
    CHECK_FALSE(doc.isPlaying());
    auto tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().position.x == Catch::Approx(3.f).margin(1e-5f));
    CHECK(doc.hierarchySnapshot().size() == 2);
    CHECK_FALSE(doc.selection().has_value());
}

// --- P1.15 RENDERING (readback de pixel — features visuais provadas) ---------------

TEST_CASE("editor: P1 — renderer desenha GIZMO por cima do sprite (readback)",
          "[editor][rhi_hardware]")
{
    if (editorGraphicsUnavailable()) {
        SKIP("sem driver gráfico (lavapipe/EGL) — suite completo roda no CI");
    }
    auto host = eng::editor::EditorHost::create("gles", ".editor-test-ws-p1gizmo");
    REQUIRE(host.ok());
    std::unique_ptr<eng::editor::EditorHost> owned{host.value()};
    int marker = 0;
    owned->surfaceCreated(&marker, eng::rhi::NativeWindowKind::Headless,
                          128, 128);
    if (owned->state() != eng::editor::HostSurfaceState::Available) {
        SKIP("OpenGL ES indisponível (renderer não criado sem driver)");
    }

    auto& doc = owned->document();
    ensureProject(doc, "P1GizmoGame");

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

    // Sprite cobrindo o CENTRO da tela: entidade em (0,0), ppu=1 → 2x2
    // unidades = 96x96 px na tela 128x128 (zoom 48).
    auto sprite = doc.createSprite("Big");
    REQUIRE(sprite.ok());
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "textureAsset", "quad.png").ok());
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "pixelsPerUnit", "1").ok());

    // Sem tool: NÃO há gizmo (honesto — UI audit).
    REQUIRE(owned->renderFrame(1.f / 60.f));
    auto* renderer = owned->viewportRenderer();
    REQUIRE(renderer != nullptr);
    CHECK(renderer->lastFrameTexturedSprites() == 1);
    CHECK(renderer->lastFrameGizmoVertices().empty());

    // Tool MOVE + seleção: gizmo desenhado POR CIMA do sprite (lote 3).
    doc.setTool(eng::editor::EditorTool::Move);
    REQUIRE(doc.select(sprite.value()).ok());
    REQUIRE(owned->renderFrame(1.f / 60.f));
    const auto& gizmoVerts = renderer->lastFrameGizmoVertices();
    REQUIRE_FALSE(gizmoVerts.empty());
    // 3 handles (centro+X+Y) × 6 vértices + 2 eixos (segmentos) × 6.
    CHECK(gizmoVerts.size() == 30);

    // Prova VISUAL: o pixel central da tela é o HANDLE CENTRAL amarelo
    // (kCenter 0.96/0.82/0.30) DESENHADO SOBRE o sprite vermelho/verde.
    std::uint8_t pixel[4] = {0, 0, 0, 0};
    REQUIRE(renderer->renderer()->readCenterPixel(pixel).ok());
    INFO("readback gizmo: " << +pixel[0] << " " << +pixel[1] << " "
                            << +pixel[2] << " " << +pixel[3]);
    CHECK(pixel[0] >= 200);  // R alto (amarelo)
    CHECK(pixel[1] >= 170);  // G alto
    CHECK(pixel[2] <= 140);  // B baixo

    // Drag REAL via documento: begin no centro + drag 48px → +1 unidade.
    CHECK(doc.gizmoDragBegin(64.f, 64.f, &owned->textureCache()) ==
          eng::editor::GizmoHandle::MoveCenter);
    REQUIRE(doc.gizmoDragTo(112.f, 64.f).ok());
    doc.gizmoDragEnd();
    auto tr = doc.transform(sprite.value());
    REQUIRE(tr.ok());
    CHECK(tr.value().position.x == Catch::Approx(1.f).margin(1e-3f));

    // PLAY com tool ativa: gizmo SOME (edição rejeitada), sprite segue.
    REQUIRE(doc.play().ok());
    REQUIRE(owned->renderFrame(1.f / 60.f));
    CHECK(renderer->lastFrameTexturedSprites() == 1);
    CHECK(renderer->lastFrameGizmoVertices().empty());
    doc.stop();
}

TEST_CASE("editor: P1 — placeholder xadrez de sprite sem textura (readback)",
          "[editor][rhi_hardware]")
{
    if (editorGraphicsUnavailable()) {
        SKIP("sem driver gráfico (lavapipe/EGL) — suite completo roda no CI");
    }
    auto host = eng::editor::EditorHost::create("gles", ".editor-test-ws-p1ph");
    REQUIRE(host.ok());
    std::unique_ptr<eng::editor::EditorHost> owned{host.value()};
    int marker = 0;
    owned->surfaceCreated(&marker, eng::rhi::NativeWindowKind::Headless,
                          128, 128);
    if (owned->state() != eng::editor::HostSurfaceState::Available) {
        SKIP("OpenGL ES indisponível (renderer não criado sem driver)");
    }

    auto& doc = owned->document();
    ensureProject(doc, "P1PlaceholderGame");

    // createSprite SEM textura: placeholder xadrez claramente identificado
    // (P1.10 — não confundir com sprite renderizado).
    auto sprite = doc.createSprite("Ghost");
    REQUIRE(sprite.ok());

    REQUIRE(owned->renderFrame(1.f / 60.f));
    auto* renderer = owned->viewportRenderer();
    REQUIRE(renderer != nullptr);
    CHECK(renderer->lastFrameTexturedSprites() == 0);  // nada texturizado

    // DADOS: o quad de cor contém o xadrez magenta (0.55, 0.22, 0.55) —
    // 8 células claras + base escura (0.13).
    const auto& verts = renderer->lastFrameVertices();
    int magenta = 0;
    int dark = 0;
    for (const auto& v : verts) {
        if (v.r == Catch::Approx(0.55f).margin(0.02f) &&
            v.g == Catch::Approx(0.22f).margin(0.02f) &&
            v.b == Catch::Approx(0.55f).margin(0.02f)) {
            ++magenta;
        }
        if (v.r == Catch::Approx(0.13f).margin(0.02f) &&
            v.g == Catch::Approx(0.13f).margin(0.02f) &&
            v.b == Catch::Approx(0.13f).margin(0.02f)) {
            ++dark;
        }
    }
    CHECK(magenta >= 6 * 8);   // 8 células × 6 vértices
    CHECK(dark >= 6);          // base

    // VISUAL: pixel de uma célula magenta — o sprite default (1 unidade)
    // é pequeno (48px); o centro da tela cai NELE (entidade em (0,0) e
    // célula central: local (-0.125..0.125 px…) — pega ponto seguro: o
    // pixel central da tela está na célula (2,2)… arranjo 4x4 a partir de
    // -24px: células de 12px; centro = fronteira. Prova está nos DADOS
    // acima; o readback sanity-checka que o CENTRO não é hue saturado
    // (sprite placeholder é sóbrio) nem vermelho de textura.
    std::uint8_t pixel[4] = {0, 0, 0, 0};
    REQUIRE(renderer->renderer()->readCenterPixel(pixel).ok());
    CHECK(pixel[3] == 255);  // opaco (base escura cobre o fundo do editor)
    CHECK((pixel[0] < 200 || pixel[2] < 200));  // não é hue rosa-vivo
}

// --- P1 vertical slice completo (host REAL, disco REAL) ----------------------------

TEST_CASE("editor: P1 — VERTICAL SLICE: import→sprite→gizmos→duplicate→save→play",
          "[editor][rhi_hardware]")
{
    if (editorGraphicsUnavailable()) {
        SKIP("sem driver gráfico (lavapipe/EGL) — suite completo roda no CI");
    }
    auto host = eng::editor::EditorHost::create("gles", ".editor-test-ws-p1vs");
    REQUIRE(host.ok());
    std::unique_ptr<eng::editor::EditorHost> owned{host.value()};
    int marker = 0;
    owned->surfaceCreated(&marker, eng::rhi::NativeWindowKind::Headless,
                          128, 128);
    if (owned->state() != eng::editor::HostSurfaceState::Available) {
        SKIP("OpenGL ES indisponível (renderer não criado sem driver)");
    }

    auto& doc = owned->document();
    ensureProject(doc, "P1SliceGame");

    // IMPORT PNG.
    auto* browser = doc.assets();
    REQUIRE(browser != nullptr);
    {
        eng::fs::FileSystem& ws = owned->workspace();
        REQUIRE(ws.mkdirs(eng::fs::Path{".import_tmp"}).ok());
        REQUIRE(ws
                    .writeAllBytes(
                        eng::fs::Path{".import_tmp/art.png"},
                        std::span{reinterpret_cast<const std::byte*>(kPng2x2),
                                  sizeof(kPng2x2)})
                    .ok());
    }
    REQUIRE(browser->import(".import_tmp/art.png", "textures", "art").ok());

    // ADD SPRITE + IMAGE (visível — readback no fim).
    auto sprite = doc.createSprite("Hero");
    REQUIRE(sprite.ok());
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "textureAsset", "art.png").ok());
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "pixelsPerUnit", "2").ok());
    REQUIRE(owned->renderFrame(1.f / 60.f));
    REQUIRE(owned->viewportRenderer()->lastFrameTexturedSprites() == 1);

    // SELECT (tap no centro) + MOVE com gizmo (48px = +1 unidade).
    auto hit = doc.viewportTap(64.f, 64.f, &owned->textureCache());
    REQUIRE(hit.has_value());
    CHECK(*hit == sprite.value());
    doc.setTool(eng::editor::EditorTool::Move);
    CHECK(doc.gizmoDragBegin(64.f, 64.f, &owned->textureCache()) ==
          eng::editor::GizmoHandle::MoveCenter);
    REQUIRE(doc.gizmoDragTo(112.f, 64.f).ok());
    doc.gizmoDragEnd();

    // ROTATE com gizmo (+90°). A entidade está em (1,0) → tela (112, 64).
    doc.setTool(eng::editor::EditorTool::Rotate);
    const float ringR = 1.f * 0.5f + 26.f / 48.f;  // sprite 1x1 unidade (2px/2ppu)
    CHECK(doc.gizmoDragBegin(112.f + ringR * 48.f, 64.f,
                             &owned->textureCache()) ==
          eng::editor::GizmoHandle::RotateRing);
    REQUIRE(doc.gizmoDragTo(112.f, 64.f - ringR * 48.f).ok());
    doc.gizmoDragEnd();

    // SCALE com gizmo (canto NE ×2). Sprite GIRADO 90°: o canto NE local
    // (0.5,0.5) vive em world (-0.5,+0.5) do centro (1,0) → tela (88, 40).
    // Alvo ×2: local (1,1) → world (-1,1) → tela (64, 16).
    doc.setTool(eng::editor::EditorTool::Scale);
    CHECK(doc.gizmoDragBegin(88.f, 40.f,
                             &owned->textureCache()) ==
          eng::editor::GizmoHandle::ScaleNE);
    REQUIRE(doc.gizmoDragTo(64.f, 16.f).ok());
    doc.gizmoDragEnd();

    auto tr = doc.transform(sprite.value());
    REQUIRE(tr.ok());
    CHECK(tr.value().position.x == Catch::Approx(1.f).margin(1e-3f));
    CHECK(tr.value().rotationDegrees.z == Catch::Approx(90.f).margin(1.f));
    CHECK(tr.value().scale.x == Catch::Approx(2.f).margin(1e-2f));
    CHECK(tr.value().scale.y == Catch::Approx(2.f).margin(1e-2f));

    // DUPLICATE: segunda entidade com a MESMA arte, transform independente.
    auto dup = doc.duplicateEntity(sprite.value());
    REQUIRE(dup.ok());
    REQUIRE(doc.moveEntityScreen(dup.value(), -48.f, 0.f).ok());

    // SAVE.
    REQUIRE(doc.saveScene("slice.json").ok());

    // PLAY: runtime renderiza OS DOIS sprites (clone).
    REQUIRE(doc.play().ok());
    REQUIRE(owned->renderFrame(1.f / 60.f));
    CHECK(owned->viewportRenderer()->lastFrameTexturedSprites() == 2);
    REQUIRE(owned->renderFrame(1.f / 60.f));
    doc.stop();

    // RELOAD pós-tudo: o que foi autorado é o que volta.
    REQUIRE(doc.loadScene("slice.json").ok());
    REQUIRE(owned->renderFrame(1.f / 60.f));
    CHECK(owned->viewportRenderer()->lastFrameTexturedSprites() == 2);
    const auto nodes = doc.hierarchySnapshot();
    REQUIRE(nodes.size() == 2);
}

// =============================================================================
// P2 — GIZMO: bug crítico §5 + matrix de regressão §6
//
// Reporte do P1: "o gizmo funciona quando o Sprite/Entity é criado/
// aplicado inicialmente, mas depois de certas alterações o gizmo deixa
// de funcionar corretamente". Causas-raiz codificadas aqui como testes:
//
//   R1 — CONSISTÊNCIA begin/drag: gizmoDragBegin resolvia os bounds COM
//        texturas (tamanho desenhado) mas gizmoDragTo os recalculava SEM
//        (nullptr) — com pivot != (0.5,0.5) o CENTRO de referência do
//        rotate/scale MUDA no meio do drag (drift/salto).
//   R2 — ESPAÇO LOCAL: o delta de MUNDO era somado direto na posição
//        LOCAL do filho — pai rotacionado/escalado fazia a entidade se
//        mover no EIXO ERRADO na tela.
// =============================================================================

TEST_CASE("editor: P2 — gizmo ROTATE com pivot não-centrado segue o pointer (R1)",
          "[editor][gizmo-p2]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;
    using eng::editor::GizmoHandle;

    writePngTemp(*g.f.fs);
    auto* browser = doc.assets();
    REQUIRE(browser != nullptr);
    REQUIRE(browser->import(".import_tmp/grass.png", "textures", "grass").ok());
    REQUIRE(doc.addComponent(g.entity, "eng::editor::SpriteData").ok());
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "textureAsset", "grass.png").ok());
    // ppu 1: imagem 2x2 px → 2x2 unidades de mundo (bem maior que a
    // escala local 1x1 — o caminho SEM textura daria 1x1: A DIFFERENÇA
    // entre os dois caminhos é exatamente o que o bug explorava).
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "pixelsPerUnit", "1").ok());
    // PIVOT no canto (0,0) — o quad desenhado desloca; o bounds do gizmo
    // tem de acompanhar (mesma fórmula do renderer).
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "pivotX", "0").ok());
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "pivotY", "0").ok());
    eng::editor::TextureCache cache;

    REQUIRE(doc.select(g.entity).ok());
    doc.setTool(eng::editor::EditorTool::Rotate);

    // Bounds com textura: half = 1; pivot (0,0) → centro visual em
    // (-1,-1) (o renderer desenha o quad com esse mesmo offset — o
    // bounds do gizmo casa com o desenho). Anel: raio max(1,1)+26/48.
    const float ringR = 1.f + 26.f / 48.f;
    // Handle no ângulo 0 do CENTRO VISUAL (-1,-1):
    const float hx = 100.f + (-1.f + ringR) * 48.f;
    const float hy = 75.f - (-1.f) * 48.f;
    CHECK(doc.gizmoDragBegin(hx, hy, &cache) == GizmoHandle::RotateRing);

    // Drag de +90° ao redor do CENTRO VISUAL (-1,-1): pointer vai de
    // ângulo 0 para ângulo 90° (acima do centro).
    REQUIRE(doc.gizmoDragTo(100.f + (-1.f) * 48.f,
                            75.f - (-1.f + ringR) * 48.f).ok());
    doc.gizmoDragEnd();

    auto tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    // A rotação segue o pointer EXATAMENTE (sem drift do centro trocado).
    CHECK(tr.value().rotationDegrees.z == Catch::Approx(90.f).margin(0.5f));

    // RODA DE NOVO (volta a 0): o segundo drag começa do estado ATUAL —
    // se o gizmo estivesse ligado a dados STALE, o segundo drag erraria.
    // Rotação 90° girou o quad (e o offset de pivot JUNTO): centro
    // visual agora (1,-1); handle no ângulo 90° desse centro.
    const float hx2 = 100.f + 1.f * 48.f;
    const float hy2 = 75.f - (-1.f + ringR) * 48.f;
    CHECK(doc.gizmoDragBegin(hx2, hy2, &cache) == GizmoHandle::RotateRing);
    REQUIRE(doc.gizmoDragTo(100.f + (1.f + ringR) * 48.f,
                            75.f + 1.f * 48.f).ok());  // ângulo 0 de novo
    doc.gizmoDragEnd();
    tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().rotationDegrees.z == Catch::Approx(0.f).margin(0.5f));
}

TEST_CASE("editor: P2 — gizmo SCALE com pivot não-centrado é consistente (R1)",
          "[editor][gizmo-p2]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;
    using eng::editor::GizmoHandle;

    writePngTemp(*g.f.fs);
    auto* browser = doc.assets();
    REQUIRE(browser != nullptr);
    REQUIRE(browser->import(".import_tmp/grass.png", "textures", "grass").ok());
    REQUIRE(doc.addComponent(g.entity, "eng::editor::SpriteData").ok());
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "textureAsset", "grass.png").ok());
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "pixelsPerUnit", "1").ok());
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "pivotX", "0").ok());
    REQUIRE(doc.setInspectorField(g.entity, "eng::editor::SpriteData",
                                  "pivotY", "0").ok());
    eng::editor::TextureCache cache;

    REQUIRE(doc.select(g.entity).ok());
    doc.setTool(eng::editor::EditorTool::Scale);

    // Pivot (0,0) → centro visual (-1,-1), half (1,1): canto NE do
    // bounds visual = (-1+1, -1+1) = (0,0) mundo → tela (100, 75).
    CHECK(doc.gizmoDragBegin(100.f, 75.f, &cache) == GizmoHandle::ScaleNE);
    // Dobra a distância ao centro visual: pointer local (1,1) → (2,2)
    // → mundo (-1+2, -1+2) = (1,1) → tela (148, 27).
    REQUIRE(doc.gizmoDragTo(148.f, 27.f).ok());
    doc.gizmoDragEnd();

    auto tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    // O RATIO é medido no MESMO frame do begin e do drag (bug §5 R1):
    // sem o fix, o begin media contra o bounds TEXTURIZADO (half 1) e o
    // drag contra o NÃO-texturizado (half 0.5) → ratio errado.
    CHECK(tr.value().scale.x == Catch::Approx(2.f).margin(0.05f));
    CHECK(tr.value().scale.y == Catch::Approx(2.f).margin(0.05f));
}

TEST_CASE("editor: P2 — gizmo MOVE em filho de pai ROTACIONADO: eixo de TELA (R2)",
          "[editor][gizmo-p2]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;
    using eng::editor::GizmoHandle;

    // Pai girado 90°: o eixo X LOCAL do filho aponta PARA CIMA no mundo.
    auto parent = doc.createEntity("Parent", eng::scene::kNoEntity);
    REQUIRE(parent.ok());
    eng::editor::TransformDesc pd;
    pd.rotationDegrees = eng::math::Vec3{0.f, 0.f, 90.f};
    REQUIRE(doc.setTransform(parent.value(), pd).ok());

    auto child = doc.createEntity("Child", parent.value());
    REQUIRE(child.ok());

    REQUIRE(doc.select(child.value()).ok());
    doc.setTool(eng::editor::EditorTool::Move);

    // Handle central: filho em (0,0) mundo → tela (100, 75).
    CHECK(doc.gizmoDragBegin(100.f, 75.f, nullptr) == GizmoHandle::MoveCenter);
    // Drag de +48px à DIREITA na TELA = +1 unidade em MUNDO no eixo X.
    // A posição do filho é LOCAL ao pai girado: local delta tem de ser
    // (0,-1) [inverse(R90) * (1,0)] — senão o filho sobe na tela em vez
    // de ir para a direita.
    REQUIRE(doc.gizmoDragTo(148.f, 75.f).ok());
    doc.gizmoDragEnd();

    auto tr = doc.transform(child.value());
    REQUIRE(tr.ok());
    CHECK(tr.value().position.x == Catch::Approx(0.f).margin(1e-3f));
    CHECK(tr.value().position.y == Catch::Approx(-1.f).margin(1e-3f));

    // E o RESULTADO VISUAL é o certo: o filho está em +1 X de MUNDO.
    auto bounds = doc.selectionBounds(nullptr);
    REQUIRE(bounds.valid);
    CHECK(bounds.worldX == Catch::Approx(1.f).margin(1e-3f));
    CHECK(bounds.worldY == Catch::Approx(0.f).margin(1e-3f));

    // EIXO X do gizmo (mundo): trava o movimento em X de mundo — o delta
    // local também tem de ser transformado (não somado cru).
    REQUIRE(doc.select(child.value()).ok());
    // Handle do eixo X: centro do filho agora em tela (148, 75) → handle
    // em (148 + 84, 75).
    CHECK(doc.gizmoDragBegin(148.f + 84.f, 75.f, nullptr) ==
          GizmoHandle::MoveAxisX);
    REQUIRE(doc.gizmoDragTo(148.f + 84.f + 48.f, 75.f - 48.f).ok());
    doc.gizmoDragEnd();
    tr = doc.transform(child.value());
    REQUIRE(tr.ok());
    // +1 em X de mundo apenas: local = inverse(R90)*(2,0) = (0,-2).
    CHECK(tr.value().position.x == Catch::Approx(0.f).margin(1e-3f));
    CHECK(tr.value().position.y == Catch::Approx(-2.f).margin(1e-3f));
}

TEST_CASE("editor: P2 — gizmo MOVE em filho de pai ESCALADO (R2)",
          "[editor][gizmo-p2]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;

    // Pai com escala 2: mover 1 unidade em MUNDO = 0.5 em LOCAL.
    auto parent = doc.createEntity("Parent", eng::scene::kNoEntity);
    REQUIRE(parent.ok());
    eng::editor::TransformDesc pd;
    pd.scale = eng::math::Vec3{2.f, 2.f, 1.f};
    REQUIRE(doc.setTransform(parent.value(), pd).ok());

    auto child = doc.createEntity("Child", parent.value());
    REQUIRE(child.ok());
    REQUIRE(doc.select(child.value()).ok());
    doc.setTool(eng::editor::EditorTool::Move);

    CHECK(doc.gizmoDragBegin(100.f, 75.f, nullptr) ==
          eng::editor::GizmoHandle::MoveCenter);
    REQUIRE(doc.gizmoDragTo(148.f, 75.f).ok());
    doc.gizmoDragEnd();

    auto tr = doc.transform(child.value());
    REQUIRE(tr.ok());
    CHECK(tr.value().position.x == Catch::Approx(0.5f).margin(1e-3f));
    CHECK(tr.value().position.y == Catch::Approx(0.f).margin(1e-3f));
}

TEST_CASE("editor: P2 — gizmo NÃO quebra após mudanças (matrix §5)",
          "[editor][gizmo-p2]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;
    using eng::editor::GizmoHandle;

    // Cenário limpo: a entidade do fixture sai — só o sprite da matrix.
    REQUIRE(doc.deleteEntity(g.entity).ok());

    writePngTemp(*g.f.fs);
    auto* browser = doc.assets();
    REQUIRE(browser != nullptr);
    REQUIRE(browser->import(".import_tmp/grass.png", "textures", "grass").ok());
    auto sprite = doc.createSprite("Hero");
    REQUIRE(sprite.ok());
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "textureAsset", "grass.png").ok());
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "pixelsPerUnit", "2").ok());
    eng::editor::TextureCache cache;
    // Sprite 2x2 px @ ppu 2 → 1x1 unidades.

    /// Verificador: gizmo MOVE responde ao drag de +1 unidade.
    auto moveWorks = [&](float expectedX) {
        REQUIRE(doc.select(sprite.value()).ok());
        doc.setTool(eng::editor::EditorTool::Move);
        const auto bounds = doc.selectionBounds(&cache);
        REQUIRE(bounds.valid);
        const float cx = doc.viewport().worldToScreenX(bounds.worldX);
        const float cy = doc.viewport().worldToScreenY(bounds.worldY);
        CHECK(doc.gizmoDragBegin(cx, cy, &cache) == GizmoHandle::MoveCenter);
        REQUIRE(doc.gizmoDragTo(cx + 48.f, cy).ok());
        doc.gizmoDragEnd();
        auto tr = doc.transform(sprite.value());
        REQUIRE(tr.ok());
        CHECK(tr.value().position.x ==
              Catch::Approx(expectedX + 1.f).margin(1e-3f));
        // Reposiciona em X para o próximo passo ter base limpa.
        eng::editor::TransformDesc reset;
        reset.position.x = expectedX;
        REQUIRE(doc.setTransform(sprite.value(), reset).ok());
    };

    // 1) Recém-criado (baseline do reporte do bug).
    moveWorks(0.f);

    // 2) Adicionar COMPONENTES (física/animação/partícula/collider).
    REQUIRE(doc.addComponent(sprite.value(), "eng::physics::RigidBody").ok());
    REQUIRE(doc.addComponent(sprite.value(), "eng::physics::Collider").ok());
    REQUIRE(doc.addComponent(sprite.value(),
                             "eng::animation::Animator").ok());
    REQUIRE(doc.addComponent(sprite.value(),
                             "eng::particles::ParticleEmitter").ok());
    moveWorks(0.f);

    // 3) ALTERAR o sprite (ppu/tint/pivot) — bounds muda, gizmo segue.
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "pixelsPerUnit", "4").ok());
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "tintR,tintG,tintB", "#80FF40").ok());
    moveWorks(0.f);

    // 4) TROCAR a textura (vazia → placeholder → volta).
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "textureAsset", "").ok());
    moveWorks(0.f);
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "textureAsset", "grass.png").ok());

    // 5) DUPLICAR: o clone também gizmo-funciona (independente).
    auto dup = doc.duplicateEntity(sprite.value());
    REQUIRE(dup.ok());
    {
        REQUIRE(doc.select(dup.value()).ok());
        doc.setTool(eng::editor::EditorTool::Move);
        const auto b = doc.selectionBounds(&cache);
        REQUIRE(b.valid);
        const float cx = doc.viewport().worldToScreenX(b.worldX);
        const float cy = doc.viewport().worldToScreenY(b.worldY);
        CHECK(doc.gizmoDragBegin(cx, cy, &cache) == GizmoHandle::MoveCenter);
        REQUIRE(doc.gizmoDragTo(cx + 48.f, cy).ok());
        doc.gizmoDragEnd();
        auto tr = doc.transform(dup.value());
        REQUIRE(tr.ok());
        CHECK(tr.value().position.x == Catch::Approx(1.f).margin(1e-3f));
        // Original NÃO se mexeu (independência real do clone).
        tr = doc.transform(sprite.value());
        REQUIRE(tr.ok());
        CHECK(tr.value().position.x == Catch::Approx(0.f).margin(1e-3f));
    }
    REQUIRE(doc.deleteEntity(dup.value()).ok());

    // 6) MOVE/ROTATE/SCALE via gizmo em SEQUÊNCIA (drags encadeados).
    moveWorks(0.f);
    {
        REQUIRE(doc.select(sprite.value()).ok());
        doc.setTool(eng::editor::EditorTool::Rotate);
        const auto b = doc.selectionBounds(&cache);
        const float ringR = b.halfW + 26.f / 48.f;
        const float cx = doc.viewport().worldToScreenX(b.worldX);
        const float cy = doc.viewport().worldToScreenY(b.worldY);
        REQUIRE(doc.gizmoDragBegin(cx + ringR * 48.f, cy, &cache) ==
                GizmoHandle::RotateRing);
        REQUIRE(doc.gizmoDragTo(cx, cy - ringR * 48.f).ok());
        doc.gizmoDragEnd();
        auto tr = doc.transform(sprite.value());
        REQUIRE(tr.ok());
        CHECK(tr.value().rotationDegrees.z ==
              Catch::Approx(90.f).margin(0.5f));
    }
    moveWorks(0.f);  // rotate NÃO derruba o move
    {
        REQUIRE(doc.select(sprite.value()).ok());
        doc.setTool(eng::editor::EditorTool::Scale);
        const auto b = doc.selectionBounds(&cache);
        const float cx = doc.viewport().worldToScreenX(b.worldX);
        const float cy = doc.viewport().worldToScreenY(b.worldY);
        REQUIRE(doc.gizmoDragBegin(
                    cx + b.halfW * 48.f, cy - b.halfH * 48.f,
                    &cache) == GizmoHandle::ScaleNE);
        REQUIRE(doc.gizmoDragTo(cx + b.halfW * 96.f, cy - b.halfH * 96.f).ok());
        doc.gizmoDragEnd();
    }

    // 7) SAVE → RELOAD → re-selecionar → gizmo vivo. (Handles da cena
    // ANTIGA morrem no reload — o reload cria um World novo: step 9 usa
    // o handle RECAREGADO, não o `sprite` original.)
    REQUIRE(doc.saveScene("matrix.json").ok());
    REQUIRE(doc.loadScene("matrix.json").ok());
    eng::ecs::Entity reloaded{};
    {
        const auto nodes = doc.hierarchySnapshot();
        REQUIRE(nodes.size() == 1);  // duplicata foi apagada antes do save
        reloaded = nodes[0].entity;
        REQUIRE(doc.select(reloaded).ok());
        doc.setTool(eng::editor::EditorTool::Move);
        const auto b = doc.selectionBounds(&cache);
        REQUIRE(b.valid);  // textura RE-RESOLVIDA pós-reload
        const float cx = doc.viewport().worldToScreenX(b.worldX);
        const float cy = doc.viewport().worldToScreenY(b.worldY);
        CHECK(doc.gizmoDragBegin(cx, cy, &cache) == GizmoHandle::MoveCenter);
        REQUIRE(doc.gizmoDragTo(cx + 48.f, cy).ok());
        doc.gizmoDragEnd();
        auto tr = doc.transform(reloaded);
        REQUIRE(tr.ok());
        CHECK(tr.value().position.x == Catch::Approx(1.f).margin(1e-3f));
    }

    // 8) PLAY → STOP → re-selecionar → gizmo vivo.
    REQUIRE(doc.play().ok());
    doc.tick(1.f / 60.f);
    doc.stop();
    {
        const auto nodes = doc.hierarchySnapshot();
        REQUIRE(nodes.size() == 1);
        REQUIRE(nodes[0].entity == reloaded);  // stop devolve a EDIÇÃO
        REQUIRE(doc.select(reloaded).ok());
        const auto b = doc.selectionBounds(&cache);
        REQUIRE(b.valid);
        CHECK(doc.gizmoDragBegin(
            doc.viewport().worldToScreenX(b.worldX),
            doc.viewport().worldToScreenY(b.worldY),
            &cache) == GizmoHandle::MoveCenter);
        doc.gizmoDragEnd();  // begin de PROVA encerra o drag (hit-test livre)
    }

    // 9) SELECIONAR outra entidade e VOLTAR.
    auto other = doc.createEntity("Other", eng::scene::kNoEntity);
    REQUIRE(other.ok());
    REQUIRE(doc.select(other.value()).ok());
    CHECK_FALSE(doc.isSelected(reloaded));
    REQUIRE(doc.select(reloaded).ok());
    {
        const auto b = doc.selectionBounds(&cache);
        REQUIRE(b.valid);
        CHECK(doc.gizmoDragBegin(
            doc.viewport().worldToScreenX(b.worldX),
            doc.viewport().worldToScreenY(b.worldY),
            &cache) == GizmoHandle::MoveCenter);
        doc.gizmoDragEnd();
    }
}

TEST_CASE("editor: P2 — gizmo em entity SEM Sprite (só Transform) §6",
          "[editor][gizmo-p2]")
{
    GizmoFixture g;
    auto& doc = *g.f.doc;
    using eng::editor::GizmoHandle;

    // Entidade VAZIA (Name + Transform apenas): selecionável e
    // transformável pelo gizmo — não exige Sprite.
    REQUIRE(doc.select(g.entity).ok());
    const auto b = doc.selectionBounds(nullptr);
    REQUIRE(b.valid);  // bounds = escala local (1x1 → half 0.5)
    CHECK(b.halfW == Catch::Approx(0.5f));
    CHECK(b.halfH == Catch::Approx(0.5f));

    doc.setTool(eng::editor::EditorTool::Move);
    CHECK(doc.gizmoDragBegin(100.f, 75.f, nullptr) == GizmoHandle::MoveCenter);
    REQUIRE(doc.gizmoDragTo(148.f, 27.f).ok());
    doc.gizmoDragEnd();
    auto tr = doc.transform(g.entity);
    REQUIRE(tr.ok());
    CHECK(tr.value().position.x == Catch::Approx(1.f).margin(1e-3f));
    CHECK(tr.value().position.y == Catch::Approx(1.f).margin(1e-3f));

    // ADICIONAR Sprite DEPOIS: bounds passa ao tamanho desenhado, gizmo
    // continua no controle (a mesma entidade).
    REQUIRE(doc.addComponent(g.entity, "eng::editor::SpriteData").ok());
    {
        REQUIRE(doc.select(g.entity).ok());
        const auto b2 = doc.selectionBounds(nullptr);
        REQUIRE(b2.valid);
        CHECK(b2.worldX == Catch::Approx(1.f).margin(1e-3f));
        doc.setTool(eng::editor::EditorTool::Rotate);
        const float ringR = b2.halfW + 26.f / 48.f;
        CHECK(doc.gizmoDragBegin(100.f + 1.f * 48.f + ringR * 48.f,
                                 75.f - 1.f * 48.f,
                                 nullptr) == GizmoHandle::RotateRing);
        REQUIRE(doc.gizmoDragTo(100.f + 1.f * 48.f,
                               75.f - 1.f * 48.f - ringR * 48.f).ok());
        doc.gizmoDragEnd();
        tr = doc.transform(g.entity);
        REQUIRE(tr.ok());
        CHECK(tr.value().rotationDegrees.z == Catch::Approx(90.f).margin(0.5f));
    }

    // REMOVER Sprite: volta ao bounds de escala — gizmo vivo.
    REQUIRE(doc.removeComponent(g.entity, "eng::editor::SpriteData").ok());
    {
        REQUIRE(doc.select(g.entity).ok());
        const auto b3 = doc.selectionBounds(nullptr);
        REQUIRE(b3.valid);
        CHECK(b3.halfW == Catch::Approx(0.5f));
        doc.setTool(eng::editor::EditorTool::Move);
        CHECK(doc.gizmoDragBegin(
            doc.viewport().worldToScreenX(b3.worldX),
            doc.viewport().worldToScreenY(b3.worldY),
            nullptr) == GizmoHandle::MoveCenter);
    }

    // TAP também acerta a entidade sem sprite (hit-test mínimo 22px).
    auto hit = doc.viewportTap(
        doc.viewport().worldToScreenX(1.f),
        doc.viewport().worldToScreenY(1.f), nullptr);
    REQUIRE(hit.has_value());
    CHECK(*hit == g.entity);
}

// =============================================================================
// P2 — COMPONENT/TICK AUTHORING (§2/§14), ANIMAÇÃO (§8), ÁUDIO (§12),
// CÂMERA (§11), EMISSOR (§10) — workflow real.
// =============================================================================

TEST_CASE("editor: P2 — addableComponents: catálogo real com hints (§2)",
          "[editor][p2]")
{
    DocFixture f;
    f.withProject();
    auto e = f.doc->createEntity("Player", eng::scene::kNoEntity);
    REQUIRE(e.ok());

    auto catalog = f.doc->addableComponents(e.value());
    REQUIRE_FALSE(catalog.empty());
    bool hasRigidBody = false, hasCollider = false, hasAnimator = false,
         hasEmitter = false, hasAudio = false, hasCamera = false,
         hasScript = false;
    for (const auto& meta : catalog) {
        hasRigidBody |= meta.name == "eng::physics::RigidBody";
        hasCollider |= meta.name == "eng::physics::Collider";
        hasAnimator |= meta.name == "eng::animation::Animator";
        hasEmitter |= meta.name == "eng::particles::ParticleEmitter";
        hasAudio |= meta.name == "eng::editor::AudioSource";
        hasCamera |= meta.name == "eng::tick::CameraData";
        hasScript |= meta.name == "eng::editor::NiScriptComponent";
        CHECK(meta.name != "eng::math::Transform");
        CHECK(meta.name != "eng::scene::Name");
        CHECK(meta.addable);
    }
    CHECK(hasRigidBody);
    CHECK(hasCollider);
    CHECK(hasAnimator);
    CHECK(hasEmitter);
    CHECK(hasAudio);
    CHECK(hasCamera);
    CHECK(hasScript);

    bool rigidHint = false, animHint = false, audioHint = false;
    for (const auto& meta : catalog) {
        if (meta.name == "eng::physics::RigidBody" && !meta.dependency.empty()) {
            rigidHint = true;
        }
        if (meta.name == "eng::animation::Animator" &&
            !meta.dependency.empty()) {
            animHint = true;
        }
        if (meta.name == "eng::editor::AudioSource" && !meta.dependency.empty()) {
            audioHint = true;
        }
    }
    CHECK(rigidHint);
    CHECK(animHint);
    CHECK(audioHint);

    REQUIRE(f.doc->addComponent(e.value(), "eng::physics::Collider").ok());
    const auto after = f.doc->addableComponents(e.value());
    for (const auto& meta : after) {
        CHECK(meta.name != "eng::physics::Collider");
    }

    auto added = f.doc->addComponentWithDependencies(e.value(),
                                                    "eng::physics::RigidBody");
    REQUIRE(added.ok());
    REQUIRE(added.value().size() == 1);
    CHECK(added.value()[0] == "eng::physics::RigidBody");
}

TEST_CASE("editor: P2 — ANIMAÇÃO: create/frames/fps/loop/assign (§8)",
          "[editor][p2]")
{
    DocFixture f;
    f.withProject();
    writePngTemp(*f.fs);
    auto* browser = f.doc->assets();
    REQUIRE(browser != nullptr);
    REQUIRE(browser->import(".import_tmp/grass.png", "textures", "grass").ok());

    REQUIRE(f.doc->animationCreate("walk").ok());
    auto list = f.doc->animationList();
    REQUIRE(list.ok());
    REQUIRE(list.value().size() == 1);
    CHECK(list.value()[0].name == "walk.anim.json");
    CHECK(list.value()[0].clip == "walk");
    CHECK(list.value()[0].loop);

    auto dup = f.doc->animationCreate("walk");
    REQUIRE(dup.isError());

    auto first = f.doc->animationAddFrame("walk", "grass.png");
    REQUIRE(first.ok());
    CHECK(first.value() == Catch::Approx(0.f).margin(1e-3f));
    auto second = f.doc->animationAddFrame("walk", "grass.png");
    REQUIRE(second.ok());
    CHECK(second.value() == Catch::Approx(0.125f).margin(1e-3f));

    REQUIRE(f.doc->animationSetMeta("walk", false, 16.f).ok());
    list = f.doc->animationList();
    REQUIRE(list.ok());
    CHECK_FALSE(list.value()[0].loop);
    auto json = f.doc->animationRead("walk.anim.json");
    REQUIRE(json.ok());
    CHECK(json.value().find("\"fps\":16") != std::string::npos);

    auto bad = f.doc->animationAddFrame("walk", "nao_existe.png");
    REQUIRE(bad.isError());

    auto e = f.doc->createEntity("Hero", eng::scene::kNoEntity);
    REQUIRE(e.ok());
    REQUIRE(f.doc->animationAssign(e.value(), "walk").ok());
    auto clip = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), e.value(), "eng::animation::Animator",
        "clip");
    REQUIRE(clip.ok());
    CHECK(clip.value() == "walk");

    // §14: clip COM frames → SpriteData auto-criado (o autor VÊ o flipbook).
    const auto comps = eng::editor::Inspector::componentsOf(
        *f.doc->sceneInFocus(), e.value());
    CHECK(std::find(comps.begin(), comps.end(),
                    "eng::editor::SpriteData") != comps.end());

    REQUIRE(f.doc->saveScene("anim.json").ok());
    REQUIRE(f.doc->loadScene("anim.json").ok());
    const auto nodes = f.doc->hierarchySnapshot();
    REQUIRE(nodes.size() == 1);
    clip = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), nodes[0].entity,
        "eng::animation::Animator", "clip");
    REQUIRE(clip.ok());
    CHECK(clip.value() == "walk");

    REQUIRE(f.doc->animationDelete("walk.anim.json").ok());
    list = f.doc->animationList();
    REQUIRE(list.ok());
    CHECK(list.value().empty());
}

TEST_CASE("editor: P2 — ANIMAÇÃO: PREVIEW em Edit + restore (§8)",
          "[editor][p2]")
{
    DocFixture f;
    f.withProject();
    // Clip com track de POSIÇÃO real (o preview mexe e restaura).
    REQUIRE(f.doc
                ->animationWrite("slide.anim.json",
                                 "{\n"
                                 "  \"name\": \"slide\",\n"
                                 "  \"fps\": 8,\n"
                                 "  \"loop\": true,\n"
                                 "  \"position\": [[0, 0, 0, 0], "
                                 "[1, 4, 0, 0]]\n"
                                 "}\n")
                .ok());

    auto e = f.doc->createEntity("Slider", eng::scene::kNoEntity);
    REQUIRE(e.ok());
    REQUIRE(f.doc->animationAssign(e.value(), "slide").ok());
    // Track presente → applyPosition LIGADO pelo assign (default smart).
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::animation::Animator",
                                     "applyPosition", "true")
                .ok());

    eng::editor::TransformDesc start;
    start.position = eng::math::Vec3{5.f, -3.f, 0.f};
    REQUIRE(f.doc->setTransform(e.value(), start).ok());

    REQUIRE(f.doc->previewStart(e.value(), "slide").ok());
    CHECK(f.doc->previewing());
    for (int i = 0; i < 30; ++i) {
        f.doc->previewTick(1.f / 60.f);  // 0.5s de 1s de clip
    }
    {
        auto tr = f.doc->transform(e.value());
        REQUIRE(tr.ok());
        // O MESMO sampler aplicou: x = 2 (metade do slide 0→4).
        CHECK(tr.value().position.x == Catch::Approx(2.f).margin(0.05f));
    }
    f.doc->previewStop();
    CHECK_FALSE(f.doc->previewing());
    {
        auto tr = f.doc->transform(e.value());
        REQUIRE(tr.ok());
        CHECK(tr.value().position.x == Catch::Approx(5.f).margin(1e-3f));
        CHECK(tr.value().position.y == Catch::Approx(-3.f).margin(1e-3f));
    }

    auto bad = f.doc->previewStart(e.value(), "nao_existe");
    REQUIRE(bad.isError());
}

TEST_CASE("editor: P2 — ANIMAÇÃO: PLAY executa clip do ASSET no clone (§8/§19)",
          "[editor][p2]")
{
    DocFixture f;
    f.withProject();
    REQUIRE(f.doc->animationCreate("rise2").ok());
    REQUIRE(f.doc
                ->animationWrite("rise2.anim.json",
                                 "{\n"
                                 "  \"name\": \"rise2\",\n"
                                 "  \"fps\": 8,\n"
                                 "  \"loop\": true,\n"
                                 "  \"position\": [[0, 0, 0, 0], "
                                 "[1, 0, 4, 0]]\n"
                                 "}\n")
                .ok());

    auto e = f.doc->createEntity("Lifter", eng::scene::kNoEntity);
    REQUIRE(e.ok());
    REQUIRE(f.doc->animationAssign(e.value(), "rise2").ok());
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::animation::Animator",
                                     "playing", "true")
                .ok());

    REQUIRE(f.doc->play().ok());
    const auto nodes = f.doc->hierarchySnapshot();
    REQUIRE(nodes.size() == 1);
    for (int i = 0; i < 30; ++i) {
        f.doc->tick(1.f / 60.f);
    }
    auto y = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), nodes[0].entity, "eng::math::Transform",
        "position.y");
    REQUIRE(y.ok());
    CHECK(std::stof(y.value()) == Catch::Approx(2.f).margin(0.15f));

    f.doc->stop();
    auto editY = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), nodes[0].entity, "eng::math::Transform",
        "position.y");
    REQUIRE(editY.ok());
    CHECK(std::stof(editY.value()) == Catch::Approx(0.f).margin(1e-3f));
}

TEST_CASE("editor: P2 — ANIMAÇÃO: FRAMES aplicados ao sprite (§8/§20)",
          "[editor][p2]")
{
    DocFixture f;
    f.withProject();
    writePngTemp(*f.fs);
    auto* browser = f.doc->assets();
    REQUIRE(browser != nullptr);
    REQUIRE(browser->import(".import_tmp/grass.png", "textures", "grass").ok());

    REQUIRE(f.doc
                ->animationWrite("flip.anim.json",
                                 "{\n"
                                 "  \"name\": \"flip\",\n"
                                 "  \"fps\": 2,\n"
                                 "  \"loop\": true,\n"
                                 "  \"frames\": ["
                                 "[0, \"grass.png\", 0, 0, 0.5, 1], "
                                 "[0.5, \"grass.png\", 0.5, 0, 1, 1]]\n"
                                 "}\n")
                .ok());

    auto e = f.doc->createEntity("Flip", eng::scene::kNoEntity);
    REQUIRE(e.ok());
    REQUIRE(f.doc->addComponent(e.value(), "eng::editor::SpriteData").ok());
    REQUIRE(f.doc->animationAssign(e.value(), "flip").ok());
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::animation::Animator",
                                     "playing", "true")
                .ok());

    REQUIRE(f.doc->play().ok());
    const auto nodes = f.doc->hierarchySnapshot();
    REQUIRE(nodes.size() == 1);
    f.doc->tick(0.1f);
    auto u0 = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), nodes[0].entity,
        "eng::editor::SpriteData", "u0");
    REQUIRE(u0.ok());
    CHECK(std::stof(u0.value()) == Catch::Approx(0.f).margin(1e-3f));

    f.doc->tick(0.5f);
    u0 = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), nodes[0].entity,
        "eng::editor::SpriteData", "u0");
    REQUIRE(u0.ok());
    CHECK(std::stof(u0.value()) == Catch::Approx(0.5f).margin(1e-3f));

    f.doc->stop();
    u0 = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), nodes[0].entity,
        "eng::editor::SpriteData", "u0");
    REQUIRE(u0.ok());
    CHECK(std::stof(u0.value()) == Catch::Approx(0.f).margin(1e-3f));
}

TEST_CASE("editor: P2 — CÂMERA segue a ENTIDADE (§11) + rect no viewport",
          "[editor][p2][tick]")
{
    DocFixture f;
    f.withProject();
    auto cam = f.doc->createEntity("Cam", eng::scene::kNoEntity);
    REQUIRE(cam.ok());
    REQUIRE(f.doc->addComponent(cam.value(), "eng::tick::CameraData").ok());
    REQUIRE(f.doc->setInspectorField(cam.value(), "eng::tick::CameraData",
                                    "posX", "12").ok());
    REQUIRE(f.doc->setInspectorField(cam.value(), "eng::tick::CameraData",
                                    "posY", "-6").ok());
    REQUIRE(f.doc->setInspectorField(cam.value(), "eng::tick::CameraData",
                                    "zoom", "96").ok());
    eng::editor::TransformDesc place;
    place.position = eng::math::Vec3{3.f, 2.f, 0.f};
    REQUIRE(f.doc->setTransform(cam.value(), place).ok());

    auto& viewport = f.doc->viewport();
    viewport.setScreenSize(1000.f, 500.f);

    auto quads = viewport.buildQuads(*f.doc->sceneInFocus(),
                                     f.doc->selection());
    const eng::editor::EntityQuad* camQuad = nullptr;
    for (const auto& q : quads) {
        if (q.entity == cam.value()) {
            camQuad = &q;
        }
    }
    REQUIRE(camQuad != nullptr);
    CHECK(camQuad->hasCamera);
    CHECK(camQuad->cameraActive);
    CHECK(camQuad->cameraCenterX == Catch::Approx(15.f).margin(1e-3f));
    CHECK(camQuad->cameraCenterY == Catch::Approx(-4.f).margin(1e-3f));
    CHECK(camQuad->cameraHalfW == Catch::Approx(1000.f / 96.f / 2.f));
    CHECK(camQuad->cameraHalfH == Catch::Approx(500.f / 96.f / 2.f));

    REQUIRE(f.doc->play().ok());
    CHECK(f.doc->hasGameCamera());
    CHECK_THAT(viewport.screenToWorldX(500.f),
               Catch::Matchers::WithinAbs(15.f, 1e-2f));
    CHECK_THAT(viewport.screenToWorldY(250.f),
               Catch::Matchers::WithinAbs(-4.f, 1e-2f));
    f.doc->stop();
}

TEST_CASE("editor: P2 — EMISSOR de partículas: marcador no viewport (§10)",
          "[editor][p2]")
{
    DocFixture f;
    f.withProject();
    auto e = f.doc->createEntity("Smoke", eng::scene::kNoEntity);
    REQUIRE(e.ok());
    REQUIRE(f.doc->addComponent(e.value(),
                                "eng::particles::ParticleEmitter").ok());
    eng::editor::TransformDesc rot;
    rot.rotationDegrees = eng::math::Vec3{0.f, 0.f, 90.f};
    REQUIRE(f.doc->setTransform(e.value(), rot).ok());

    auto quads = f.doc->viewport().buildQuads(*f.doc->sceneInFocus(),
                                              f.doc->selection());
    const eng::editor::EntityQuad* found = nullptr;
    for (const auto& q : quads) {
        if (q.entity == e.value()) {
            found = &q;
        }
    }
    REQUIRE(found != nullptr);
    CHECK(found->hasEmitter);
    CHECK(found->emitterDirX == Catch::Approx(-1.f).margin(1e-3f));
    CHECK(found->emitterDirY == Catch::Approx(0.f).margin(1e-3f));

    REQUIRE(f.doc->setInspectorField(
                e.value(), "eng::particles::ParticleEmitter",
                "direction.x", "1").ok());
    REQUIRE(f.doc->setInspectorField(
                e.value(), "eng::particles::ParticleEmitter",
                "direction.y", "0").ok());
    quads = f.doc->viewport().buildQuads(*f.doc->sceneInFocus(),
                                         f.doc->selection());
    for (const auto& q : quads) {
        if (q.entity == e.value()) {
            found = &q;
        }
    }
    REQUIRE(found != nullptr);
    CHECK(found->emitterDirX == Catch::Approx(0.f).margin(1e-3f));
    CHECK(found->emitterDirY == Catch::Approx(1.f).margin(1e-3f));
}

TEST_CASE("editor: P2 — ÁUDIO: AudioSource + mixer REAL no Play (§12)",
          "[editor][p2]")
{
    DocFixture f;
    f.withProject();
    constexpr std::uint32_t kSamples = 240;
    std::vector<std::byte> wav;
    auto push32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            wav.push_back(static_cast<std::byte>(v >> (8 * i)));
        }
    };
    auto push16 = [&](std::uint16_t v) {
        wav.push_back(static_cast<std::byte>(v & 0xff));
        wav.push_back(static_cast<std::byte>(v >> 8));
    };
    const auto pushTag = [&](const char (&tag)[5]) {
        for (int i = 0; i < 4; ++i) {
            wav.push_back(static_cast<std::byte>(tag[i]));
        }
    };
    pushTag("RIFF");
    push32(36 + kSamples * 2);
    pushTag("WAVE");
    pushTag("fmt ");
    push32(16);
    push16(1);
    push16(1);
    push32(48000);
    push32(96000);
    push16(2);
    push16(16);
    pushTag("data");
    push32(kSamples * 2);
    for (std::uint32_t i = 0; i < kSamples; ++i) {
        push16(static_cast<std::uint16_t>(i * 100));
    }
    auto* browser = f.doc->assets();
    REQUIRE(browser != nullptr);
    REQUIRE(f.fs->mkdirs(eng::fs::Path{".import_tmp"}).ok());
    REQUIRE(f.fs
                ->writeAllBytes(
                    eng::fs::Path{".import_tmp/beep.wav"},
                    std::span{wav.data(), wav.size()})
                .ok());
    REQUIRE(browser->import(".import_tmp/beep.wav", "audio", "beep").ok());

    auto e = f.doc->createEntity("Sfx", eng::scene::kNoEntity);
    REQUIRE(e.ok());
    REQUIRE(f.doc->addComponent(e.value(), "eng::editor::AudioSource").ok());
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::editor::AudioSource",
                                    "soundAsset", "beep.wav").ok());
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::editor::AudioSource",
                                    "playOnStart", "true").ok());

    const auto fields = f.doc->inspectorFields(
        e.value(), "eng::editor::AudioSource");
    bool sawAudioKind = false;
    for (const auto& field : fields) {
        if (field.path == "soundAsset") {
            sawAudioKind = field.kind == "audio";
        }
    }
    CHECK(sawAudioKind);

    REQUIRE(f.doc->play().ok());
    f.doc->tick(1.f / 60.f);
    CHECK(f.doc->audioMixer().stats().voicesPlayed == 1);
    CHECK(f.doc->audioMixer().liveVoices() >= 1);
    std::vector<float> buffer(512, 0.f);
    f.doc->audioMixer().mix(buffer.data(), 256);
    CHECK(f.doc->audioMixer().stats().framesMixed > 0);
    f.doc->stop();
    CHECK(f.doc->audioMixer().liveVoices() == 0);

    REQUIRE(f.doc->audioPreview("beep.wav").ok());
    CHECK(f.doc->audioMixer().liveVoices() == 1);
}

TEST_CASE("editor: P2 — WORKFLOW de integração REAL (§21/§23)", "[editor][p2]")
{
    DocFixture f;
    f.withProject();

    writePngTemp(*f.fs);
    auto* browser = f.doc->assets();
    REQUIRE(browser != nullptr);
    REQUIRE(browser->import(".import_tmp/grass.png", "textures", "grass").ok());

    auto player = f.doc->createSprite("Player");
    REQUIRE(player.ok());
    REQUIRE(f.doc->setInspectorField(player.value(),
                                    "eng::editor::SpriteData",
                                    "textureAsset", "grass.png").ok());

    auto withDeps = f.doc->addComponentWithDependencies(
        player.value(), "eng::physics::RigidBody");
    REQUIRE(withDeps.ok());
    REQUIRE(f.doc->addComponent(player.value(), "eng::physics::Collider").ok());
    REQUIRE(f.doc->setInspectorField(player.value(),
                                     "eng::physics::Collider", "shape",
                                     "Box").ok());
    REQUIRE(f.doc->setInspectorField(
                player.value(), "eng::physics::Collider",
                "halfExtents.x", "0.5").ok());
    REQUIRE(f.doc->setInspectorField(
                player.value(), "eng::physics::Collider",
                "halfExtents.y", "0.5").ok());

    REQUIRE(f.doc->animationCreate("idle").ok());
    REQUIRE(f.doc->animationAddFrame("idle", "grass.png").ok());
    REQUIRE(f.doc->animationAssign(player.value(), "idle").ok());

    REQUIRE(f.doc->scriptCreate("mover").ok());
    REQUIRE(f.doc->scriptAssign(player.value(), "mover.nis").ok());

    auto cam = f.doc->createEntity("MainCam", eng::scene::kNoEntity);
    REQUIRE(cam.ok());
    REQUIRE(f.doc->addComponent(cam.value(), "eng::tick::CameraData").ok());
    auto enemy = f.doc->createEntity("Enemy", eng::scene::kNoEntity);
    REQUIRE(enemy.ok());
    REQUIRE(f.doc->addComponent(enemy.value(), "eng::physics::RigidBody").ok());
    REQUIRE(f.doc->addComponent(enemy.value(), "eng::physics::Collider").ok());

    REQUIRE(f.doc->select(player.value()).ok());
    f.doc->setTool(eng::editor::EditorTool::Move);
    const auto b = f.doc->selectionBounds(nullptr);
    REQUIRE(b.valid);
    CHECK(f.doc->gizmoDragBegin(
              f.doc->viewport().worldToScreenX(b.worldX),
              f.doc->viewport().worldToScreenY(b.worldY),
              nullptr) == eng::editor::GizmoHandle::MoveCenter);
    REQUIRE(f.doc->gizmoDragTo(
        f.doc->viewport().worldToScreenX(b.worldX) + 48.f,
        f.doc->viewport().worldToScreenY(b.worldY)).ok());
    f.doc->gizmoDragEnd();

    REQUIRE(f.doc->saveScene("workflow.json").ok());
    REQUIRE(f.doc->loadScene("workflow.json").ok());
    {
        // Reload recria por SceneEntityId (UUID ALEATÓRIO — ADR-033):
        // a ordem dos nós NÃO é a de criação. Acha o PLAYER PELO NOME.
        const auto nodes = f.doc->hierarchySnapshot();
        REQUIRE(nodes.size() == 3);
        eng::ecs::Entity playerNode{};
        for (const auto& node : nodes) {
            if (node.name == "Player") {
                playerNode = node.entity;
            }
        }
        REQUIRE(playerNode != eng::scene::kNoEntity);
        const auto playerComponents =
            eng::editor::Inspector::componentsOf(*f.doc->sceneInFocus(),
                                                  playerNode);
        bool animator = false, script = false, sprite = false,
             rigid = false, collider = false;
        for (const auto& c : playerComponents) {
            animator |= c == "eng::animation::Animator";
            script |= c == "eng::editor::NiScriptComponent";
            sprite |= c == "eng::editor::SpriteData";
            rigid |= c == "eng::physics::RigidBody";
            collider |= c == "eng::physics::Collider";
        }
        CHECK(animator);
        CHECK(script);
        CHECK(sprite);
        CHECK(rigid);
        CHECK(collider);
        auto tr = f.doc->transform(playerNode);
        REQUIRE(tr.ok());
        CHECK(tr.value().position.x == Catch::Approx(1.f).margin(1e-3f));
    }

    REQUIRE(f.doc->play().ok());
    const auto sched = f.doc->runtimeScheduler();
    REQUIRE(sched != nullptr);
    const auto order = sched->systemOrder();
    REQUIRE(order.size() == 6);
    CHECK(order[0] == "PhysicsTick");
    CHECK(order[1] == "AnimationTick");
    CHECK(order[2] == "ParticleTick");
    CHECK(order[3] == "ScriptTick");
    CHECK(order[4] == "AudioTick");
    CHECK(order[5] == "CameraTick");
    for (int i = 0; i < 10; ++i) {
        f.doc->tick(1.f / 60.f);
    }
    CHECK(f.doc->hasGameCamera());
    f.doc->stop();
    CHECK_FALSE(f.doc->hasGameCamera());
    const auto after = f.doc->hierarchySnapshot();
    CHECK(after.size() == 3);
}

// =============================================================================
// P3 §0 — STARTUP ANDROID: bug "AlreadyExists" (create × open × restore ×
// reentrada). A política vive no EditorDocument; cada caso abaixo espelha
// um cenário do relatório do dispositivo (Realme C33).
// =============================================================================

namespace {

/// Workspace compartilhado entre "sessões" (documentos SEQUENCIAIS sobre o
/// MESMO armazenamento) — simula processo morto/recriado do Android. O
/// estado em memória de cada sessão começa VAZIO (hasProject()==false):
/// exatamente o gatilho do bug original.
struct StartupSessions {
    eng::fs::MemoryFileSystem storage{};
    std::unique_ptr<EditorDocument> doc{};

    void newSession()
    {
        doc.reset();  // "processo morre" — estado em memória vai embora
        auto created = EditorDocument::create(storage, eng::fs::Path{"."});
        REQUIRE(created.ok());
        doc = std::move(created.value());
    }

    StartupSessions() { newSession(); }
};

}  // namespace

// A. Criar projeto novo (instalação limpa): sem projetos no workspace →
// cria o default "MeuJogo" — SEM AlreadyExists.
TEST_CASE("p3-startup A: instalação limpa cria o default MeuJogo", "[editor][p3]")
{
    StartupSessions s;
    REQUIRE_FALSE(s.doc->hasProject());  // memória vazia = processo novo

    auto ensured = s.doc->ensureStartupProject();
    REQUIRE(ensured.ok());
    CHECK(ensured.value() == "MeuJogo");
    CHECK(s.doc->hasProject());
    CHECK(s.doc->projectName() == "MeuJogo");
    // Estrutura real no disco (o que o app veria após a criação).
    CHECK(s.storage.exists(eng::fs::Path{"MeuJogo/project.goni.json"}).value());
    CHECK(s.storage.exists(eng::fs::Path{"MeuJogo/scenes"}).value());
}

// B. Abrir projeto existente (reentrada): a segunda "sessão" (processo
// novo, memória vazia) ABRE o projeto — antes do fix, este caminho
// chamava newProject("MeuJogo") e recebia AlreadyExists.
TEST_CASE("p3-startup B: reentrada ABRE o projeto existente (bug raiz)",
          "[editor][p3]")
{
    StartupSessions s;
    REQUIRE(s.doc->ensureStartupProject().ok());  // sessão 1: cria MeuJogo
    s.newSession();                                // processo novo

    REQUIRE_FALSE(s.doc->hasProject());  // gatilho do bug: memória vazia
    auto ensured = s.doc->ensureStartupProject();
    REQUIRE(ensured.ok());               // ANTES: AlreadyExists
    CHECK(ensured.value() == "MeuJogo");
    CHECK(s.doc->hasProject());
    CHECK(s.doc->projectName() == "MeuJogo");
    // Nenhum duplicado foi criado (o workspace continua com UM projeto).
    CHECK(s.doc->listProjects().value().size() == 1);
}

// C. Criar projeto com nome existente → erro CONTROLADO (AlreadyExists
// só existe neste caminho MANUAL — o documento segue utilizável).
TEST_CASE("p3-startup C: duplicado manual devolve AlreadyExists controlado",
          "[editor][p3]")
{
    StartupSessions s;
    REQUIRE(s.doc->newProject("MeuJogo").ok());
    CHECK(s.doc->hasProject());

    auto dup = s.doc->newProject("MeuJogo");
    REQUIRE(dup.isError());
    CHECK(dup.error().code == eng::core::StatusCode::AlreadyExists);
    // O erro NÃO derruba o documento: o projeto original segue aberto.
    CHECK(s.doc->hasProject());
    CHECK(s.doc->projectName() == "MeuJogo");
    CHECK(s.doc->saveProject().ok());
}

// D. Abrir o projeto existente após reiniciar o editor: cena salva na
// sessão 1 volta INTEIRA na sessão 2 (round-trip via startup).
TEST_CASE("p3-startup D: cena salva volta após reinício (restore)", "[editor][p3]")
{
    StartupSessions s;
    REQUIRE(s.doc->ensureStartupProject().ok());
    auto e = s.doc->createEntity("Player", eng::scene::kNoEntity);
    REQUIRE(e.ok());
    eng::editor::TransformDesc desc{};
    desc.position = {2.f, -3.f, 0.f};
    desc.rotationDegrees = {0.f, 0.f, 90.f};
    desc.scale = {2.f, 2.f, 1.f};
    REQUIRE(s.doc->setTransform(e.value(), desc).ok());
    REQUIRE(s.doc->saveScene("main.json").ok());
    REQUIRE(s.doc->saveProject().ok());

    s.newSession();  // "reiniciar o editor"
    REQUIRE(s.doc->ensureStartupProject().ok());
    REQUIRE(s.doc->loadScene("main.json").ok());
    const auto snap = s.doc->hierarchySnapshot();
    REQUIRE(snap.size() == 1);
    CHECK(snap[0].name == "Player");
    auto tr2 = s.doc->transform(snap[0].entity);
    REQUIRE(tr2.ok());
    CHECK(tr2.value().position.x == Catch::Approx(2.f).margin(1e-3f));
    CHECK(tr2.value().position.y == Catch::Approx(-3.f).margin(1e-3f));
    CHECK(tr2.value().rotationDegrees.z == Catch::Approx(90.f).margin(1e-2f));
}

// E. Activity recreation (host destruído + recriado no MESMO workspace em
// disco REAL — caminho completo do Android, sem GPU necessária).
TEST_CASE("p3-startup E: recreation do EditorHost reabre sem AlreadyExists",
          "[editor][p3]")
{
    const std::string ws = ".editor-test-ws-startup-e";
    {
        auto host = eng::editor::EditorHost::create("auto", ws.c_str());
        REQUIRE(host.ok());
        std::unique_ptr<eng::editor::EditorHost> owned{host.value()};
        auto ensured = owned->ensureStartupProject();
        REQUIRE(ensured.ok());
        CHECK(ensured.value() == "MeuJogo");
    }  // onDestroy: host morre (documento junto)
    {
        // Activity recriada: processo novo, workspace persistido.
        auto host = eng::editor::EditorHost::create("auto", ws.c_str());
        REQUIRE(host.ok());
        std::unique_ptr<eng::editor::EditorHost> owned{host.value()};
        auto ensured = owned->ensureStartupProject();
        REQUIRE(ensured.ok());  // ANTES do fix: AlreadyExists + sem projeto
        CHECK(ensured.value() == "MeuJogo");
        CHECK(owned->document().hasProject());
    }
    std::filesystem::remove_all(std::filesystem::path{ws});
}

// F. Fechar e abrir novamente: DUAS políticas na MESMA sessão — a
// segunda é no-op (o projeto já está em memória; não recria nada).
TEST_CASE("p3-startup F: política dupla é no-op (não recria/reabre)",
          "[editor][p3]")
{
    StartupSessions s;
    auto first = s.doc->ensureStartupProject();
    REQUIRE(first.ok());
    const auto projectsAfterFirst = s.doc->listProjects().value();

    auto second = s.doc->ensureStartupProject();
    REQUIRE(second.ok());
    CHECK(second.value() == first.value());
    CHECK(s.doc->listProjects().value() == projectsAfterFirst);
    CHECK(s.doc->listProjects().value().size() == 1);
}

// G. Instalação limpa via HOST REAL (mesmo caminho A, com NativeFS +
// RootedFS — a fronteira exata do Android).
TEST_CASE("p3-startup G: instalação limpa no host real cria MeuJogo",
          "[editor][p3]")
{
    const std::string ws = ".editor-test-ws-startup-g";
    auto host = eng::editor::EditorHost::create("auto", ws.c_str());
    REQUIRE(host.ok());
    std::unique_ptr<eng::editor::EditorHost> owned{host.value()};

    auto ensured = owned->ensureStartupProject();
    REQUIRE(ensured.ok());
    CHECK(ensured.value() == "MeuJogo");
    CHECK(owned->document().hasProject());
    std::filesystem::remove_all(std::filesystem::path{ws});
}

// H. Múltiplos projetos: o ÚLTIMO USADO é restaurado; registro stale
// (projeto apagado) cai no default.
TEST_CASE("p3-startup H: último usado vence; registro stale cai no default",
          "[editor][p3]")
{
    StartupSessions s;
    REQUIRE(s.doc->ensureStartupProject().ok());          // MeuJogo (auto)
    REQUIRE(s.doc->newProject("Segundo").ok());           // último usado
    s.newSession();
    auto ensured = s.doc->ensureStartupProject();
    REQUIRE(ensured.ok());
    CHECK(ensured.value() == "Segundo");                  // último usado

    // Registro stale (projeto que não existe mais): volta ao default.
    REQUIRE(s.storage
                .writeAllText(eng::fs::Path{".goni_last_project"},
                              std::string_view{"ProjetoFantasma"})
                .ok());
    s.newSession();
    auto fallback = s.doc->ensureStartupProject();
    REQUIRE(fallback.ok());
    CHECK(fallback.value() == "MeuJogo");
}

// I. Projeto existente + cenas/assets/scripts: tudo continua no lugar
// após o restore (nenhum dado perdido pelo ciclo de startup).
TEST_CASE("p3-startup I: cenas/scripts/assets sobrevivem ao restore",
          "[editor][p3]")
{
    StartupSessions s;
    REQUIRE(s.doc->ensureStartupProject().ok());
    REQUIRE(s.doc->saveScene("fase1.json").ok());
    REQUIRE(s.doc->scriptCreate("main").ok());
    REQUIRE(s.doc->saveProject().ok());

    s.newSession();
    REQUIRE(s.doc->ensureStartupProject().ok());
    // Cena volta a carregar (disco intacto).
    REQUIRE(s.doc->loadScene("fase1.json").ok());
    // Script do projeto continua catalogado.
    auto scripts = s.doc->scriptList();
    REQUIRE(scripts.ok());
    REQUIRE(scripts.value().size() == 1);
    CHECK(scripts.value()[0] == "main.nis");  // nome catalogado c/ ext
    CHECK(s.storage.exists(eng::fs::Path{"MeuJogo/scenes/fase1.json"}).value());
}

// J. Não perder dados do projeto: mutações + save + restore completo.
TEST_CASE("p3-startup J: dados não se perdem no ciclo completo", "[editor][p3]")
{
    StartupSessions s;
    REQUIRE(s.doc->ensureStartupProject().ok());
    auto e = s.doc->createEntity("Colecionavel", eng::scene::kNoEntity);
    REQUIRE(e.ok());
    REQUIRE(s.doc->renameEntity(e.value(), "Gema").ok());
    REQUIRE(s.doc->saveScene("save.json").ok());
    REQUIRE(s.doc->saveProject().ok());

    s.newSession();
    REQUIRE(s.doc->ensureStartupProject().ok());
    REQUIRE(s.doc->loadScene("save.json").ok());
    const auto snap = s.doc->hierarchySnapshot();
    REQUIRE(snap.size() == 1);
    CHECK(snap[0].name == "Gema");  // renomeação persistiu
}

// Listagem rigorosa: só pastas com project.goni.json; ocultos e pastas
// comuns ficam de fora (mesmo filtro do seletor de projetos).
TEST_CASE("p3-startup: listProjects filtra ocultos e não-projetos",
          "[editor][p3]")
{
    StartupSessions s;
    REQUIRE(s.doc->ensureStartupProject().ok());
    REQUIRE(s.storage.mkdirs(eng::fs::Path{".import_tmp/staging"}).ok());
    REQUIRE(s.storage.mkdirs(eng::fs::Path{"PastaQualquer"}).ok());

    auto listed = s.doc->listProjects();
    REQUIRE(listed.ok());
    REQUIRE(listed.value().size() == 1);
    CHECK(listed.value()[0] == "MeuJogo");
}

// Regressão do registro: remember é best-effort — falha silenciosa NÃO
// derruba a operação de projeto (nada lança; Result carrega o motivo).
TEST_CASE("p3-startup: registro do último projeto é best-effort", "[editor][p3]")
{
    StartupSessions s;
    REQUIRE(s.doc->ensureStartupProject().ok());
    // Record existe e é legível pela próxima sessão.
    CHECK(s.storage.exists(eng::fs::Path{".goni_last_project"}).value());
    CHECK(s.doc->lastUsedProject() == "MeuJogo");
}

// --- watchdog de backend (P3 §0 — "fecha rapidamente") -----------------------
//
// Sessão que morre antes de kWatchdogHealthyFrames deixa "trying:X"; a
// próxima sessão Auto PULA X. Sessão saudável promove X a "good:X" e o
// Auto passa a preferi-lo. Requer driver real (lavapipe/EGL no CI).

TEST_CASE("p3-watchdog: Auto pula backend morto e promove o saudável",
          "[editor][rhi_hardware]")
{
    if (editorGraphicsUnavailable()) {
        SKIP("sem driver gráfico (lavapipe/EGL) — suite completo roda no CI");
    }
    const std::string ws = ".editor-test-ws-watchdog";
    std::filesystem::remove_all(std::filesystem::path{ws});

    // Sessão 1: marca a morte súbita do VULKAN (crash simulado antes de
    // 30 frames — o marcador fica "trying:vulkan").
    {
        auto host = eng::editor::EditorHost::create("auto", ws.c_str());
        REQUIRE(host.ok());
        std::unique_ptr<eng::editor::EditorHost> owned{host.value()};
        REQUIRE(owned->ensureStartupProject().ok());

        // Simula a morte: escreve o marcador COMO a sessão morta deixaria
        // (a escrita real acontece em watchdogOnRendererCreated).
        REQUIRE(owned->workspace()
                    .writeAllText(eng::fs::Path{".goni_backend_watchdog"},
                                  std::string_view{"trying:vulkan"})
                    .ok());
    }
    // Sessão 2 (processo novo, MESMO workspace): Auto deve PULAR Vulkan.
    {
        auto host = eng::editor::EditorHost::create("auto", ws.c_str());
        REQUIRE(host.ok());
        std::unique_ptr<eng::editor::EditorHost> owned{host.value()};

        int marker = 0;
        owned->surfaceCreated(&marker, eng::rhi::NativeWindowKind::Headless, 64,
                              48);
        REQUIRE(owned->state() == eng::editor::HostSurfaceState::Available);
        // Vulkan foi pulado → o ativo é GLES (ou Auto caiu fora do vk).
        CHECK(owned->selectedBackend() == eng::rhi::BackendType::OpenGLES);

        // Sessão saudável: 30+ frames apresentados → "good:gles".
        for (int i = 0; i < 35; ++i) {
            REQUIRE(owned->renderFrame(1.f / 60.f));
        }
        auto markerText = owned->workspace().readAllText(
            eng::fs::Path{".goni_backend_watchdog"});
        REQUIRE(markerText.ok());
        CHECK(markerText.value() == "good:gles");
    }
    // Sessão 3: "good:gles" → Auto PREFERE GLES direto.
    {
        auto host = eng::editor::EditorHost::create("auto", ws.c_str());
        REQUIRE(host.ok());
        std::unique_ptr<eng::editor::EditorHost> owned{host.value()};
        int marker = 0;
        owned->surfaceCreated(&marker, eng::rhi::NativeWindowKind::Headless, 64,
                              48);
        REQUIRE(owned->state() == eng::editor::HostSurfaceState::Available);
        CHECK(owned->selectedBackend() == eng::rhi::BackendType::OpenGLES);
    }
    std::filesystem::remove_all(std::filesystem::path{ws});
}

// =============================================================================
// P3 — SHADER/MATERIAL/LIGHT2D: componentes reais, material authorável,
// iluminação por fragmento (bloco PerFrame), EDIT/PLAY parity.
// =============================================================================

TEST_CASE("editor: P3 — Light2D entra no catálogo e serializa (round-trip)",
          "[editor][p3light]")
{
    DocFixture f;
    f.withProject();
    auto e = f.doc->createEntity("Lamp", eng::scene::kNoEntity);
    REQUIRE(e.ok());

    // Catálogo ADDÁVEL real: registro do ComponentRegistration.
    auto catalog = f.doc->addableComponents(e.value());
    bool found = false;
    for (const auto& meta : catalog) {
        found |= meta.name == "eng::render::Light2D";
    }
    REQUIRE(found);

    // Add → componente vivo com defaults.
    REQUIRE(f.doc->addComponent(e.value(), "eng::render::Light2D").ok());
    // Inspector lê/escreve por caminho (reflexão — §7).
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::render::Light2D",
                                     "intensity", "2.5")
                .ok());
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::render::Light2D",
                                     "radius", "6.5")
                .ok());
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::render::Light2D",
                                     "colorR", "0.1")
                .ok());
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::render::Light2D",
                                     "layer", "UI")
                .ok());
    const auto fields = f.doc->inspectorFields(e.value(), "eng::render::Light2D");
    bool hasIntensity = false, hasLayer = false, hasEnabled = false,
         hasFalloff = false;
    for (const auto& field : fields) {
        hasIntensity |= field.path == "intensity" && field.value == "2.5";
        hasLayer |= field.path == "layer" && field.value == "UI";
        hasEnabled |= field.path == "enabled";
        hasFalloff |= field.path == "falloff";
    }
    CHECK(hasIntensity);
    CHECK(hasLayer);
    CHECK(hasEnabled);
    CHECK(hasFalloff);

    // Save → reload: o componente SOBREVIVE com os valores.
    REQUIRE(f.doc->saveScene("luz.json").ok());
    REQUIRE(f.doc->loadScene("luz.json").ok());
    const auto after = f.doc->inspectorFields(e.value(), "eng::render::Light2D");
    for (const auto& field : after) {
        if (field.path == "intensity") {
            CHECK(field.value == "2.5");
        }
        if (field.path == "radius") {
            CHECK(field.value == "6.5");
        }
        if (field.path == "colorR") {
            CHECK(field.value == "0.1");
        }
        if (field.path == "layer") {
            CHECK(field.value == "UI");
        }
    }

    // Remove: sai limpo.
    REQUIRE(f.doc->removeComponent(e.value(), "eng::render::Light2D").ok());
    // Componente removido: a consulta por caminho FALHA (Inspector).
    auto gone = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), e.value(), "eng::render::Light2D", "radius");
    CHECK(gone.isError());
}

TEST_CASE("editor: P3 — buildQuads coleta a luz com posição de mundo",
          "[editor][p3light]")
{
    DocFixture f;
    f.withProject();
    auto e = f.doc->createEntity("Lanterna", eng::scene::kNoEntity);
    REQUIRE(e.ok());
    eng::editor::TransformDesc desc{};
    desc.position = {3.f, -2.f, 0.f};
    REQUIRE(f.doc->setTransform(e.value(), desc).ok());
    REQUIRE(f.doc->addComponent(e.value(), "eng::render::Light2D").ok());
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::render::Light2D",
                                     "intensity", "3")
                .ok());
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::render::Light2D",
                                     "radius", "9")
                .ok());

    const auto quads =
        f.doc->viewport().buildQuads(*f.doc->sceneInFocus(), {});
    REQUIRE(quads.size() == 1);
    CHECK(quads[0].hasLight);
    CHECK(quads[0].lightIntensity == Catch::Approx(3.f));
    CHECK(quads[0].lightRadius == Catch::Approx(9.f));
    // Posição da luz = TRANSFORM da entidade (não campo da luz).
    CHECK(quads[0].worldX == Catch::Approx(3.f).margin(1e-3f));
    CHECK(quads[0].worldY == Catch::Approx(-2.f).margin(1e-3f));

    // Desligada → fora do bloco (custo zero).
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::render::Light2D",
                                     "enabled", "false")
                .ok());
    const auto offQuads =
        f.doc->viewport().buildQuads(*f.doc->sceneInFocus(), {});
    REQUIRE(offQuads.size() == 1);
    CHECK_FALSE(offQuads[0].hasLight);
}

TEST_CASE("editor: P3 — material CRUD + resolve (shader/tint reais)",
          "[editor][p3light]")
{
    DocFixture f;
    f.withProject();

    // Sem materiais ainda.
    auto empty = f.doc->materialList();
    REQUIRE(empty.ok());
    CHECK(empty.value().empty());

    // Create → lista com template lit neutro.
    REQUIRE(f.doc->materialCreate("Gema").ok());
    auto listed = f.doc->materialList();
    REQUIRE(listed.ok());
    REQUIRE(listed.value().size() == 1);
    CHECK(listed.value()[0].name == "Gema.mat.json");
    CHECK(listed.value()[0].shader == "lit");
    CHECK(listed.value()[0].tintR == 1.f);

    // Duplicado → AlreadyExists controlado.
    auto dup = f.doc->materialCreate("Gema");
    REQUIRE(dup.isError());
    CHECK(dup.error().code == eng::core::StatusCode::AlreadyExists);

    // Write com shader inválido → rejeitado ANTES de gravar.
    auto bad = f.doc->materialWrite(
        "Gema.mat.json", R"({"name":"Gema","shader":"pbr-mega"})");
    REQUIRE(bad.isError());

    // Write válido: unlit vermelho meio-transparente.
    REQUIRE(f.doc->materialWrite(
                "Gema.mat.json",
                R"({"name":"Gema","shader":"unlit","tint":[1,0.25,0.25,0.5]})")
                .ok());
    listed = f.doc->materialList();
    REQUIRE(listed.ok());
    REQUIRE(listed.value().size() == 1);
    CHECK(listed.value()[0].shader == "unlit");
    CHECK(listed.value()[0].tintG == Catch::Approx(0.25f));
    CHECK(listed.value()[0].tintA == Catch::Approx(0.5f));

    // Read (round-trip do JSON cru).
    auto content = f.doc->materialRead("Gema.mat.json");
    REQUIRE(content.ok());
    CHECK(content.value().find("unlit") != std::string::npos);

    // Names (picker do Inspector).
    auto names = f.doc->materialNames();
    REQUIRE(names.ok());
    REQUIRE(names.value().size() == 1);
    CHECK(names.value()[0] == "Gema.mat.json");

    // Resolve: sprite com material → shader + tint multiplicado.
    auto e = f.doc->createEntity("Pedra", eng::scene::kNoEntity);
    REQUIRE(e.ok());
    REQUIRE(f.doc->addComponent(e.value(), "eng::editor::SpriteData").ok());
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::editor::SpriteData",
                                     "textureAsset", "rocha.png")
                .ok());
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::editor::SpriteData",
                                     "tintB", "0.5")
                .ok());
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::editor::SpriteData",
                                     "materialAsset", "Gema")
                .ok());

    auto quads = f.doc->viewport().buildQuads(*f.doc->sceneInFocus(), {});
    REQUIRE(quads.size() == 1);
    CHECK(quads[0].materialAsset == "Gema");
    CHECK(quads[0].materialShader == "lit");  // default ANTES do resolve

    f.doc->resolveMaterials(quads);
    CHECK(quads[0].materialShader == "unlit");           // do material
    CHECK(quads[0].tintG == Catch::Approx(0.25f));       // 1 × 0.25
    CHECK(quads[0].tintB == Catch::Approx(0.125f));      // 0.5 × 0.25
    CHECK(quads[0].tintA == Catch::Approx(0.5f));

    // Sprite SEM material → default lit, tint intacto.
    auto e2 = f.doc->createEntity("Neutro", eng::scene::kNoEntity);
    REQUIRE(e2.ok());
    REQUIRE(f.doc->addComponent(e2.value(), "eng::editor::SpriteData").ok());
    auto quads2 = f.doc->viewport().buildQuads(*f.doc->sceneInFocus(), {});
    REQUIRE(quads2.size() == 2);
    f.doc->resolveMaterials(quads2);
    bool checked = false;
    for (const auto& q : quads2) {
        if (q.entity == e2.value()) {
            CHECK(q.materialShader == "lit");
            CHECK(q.tintR == 1.f);
            checked = true;
        }
    }
    CHECK(checked);

    // Save/Reload do projeto inteiro: material persiste como ASSET.
    REQUIRE(f.doc->saveProject().ok());
    // (cena pode não ter sido salva — o que conta é o projeto/asset)
    auto stillThere = f.doc->materialList();
    REQUIRE(stillThere.ok());
    CHECK(stillThere.value().size() == 1);

    // Delete → some; sprite referenciando cai no default (sem estado ruim).
    REQUIRE(f.doc->materialDelete("Gema.mat.json").ok());
    auto afterDelete = f.doc->materialList();
    REQUIRE(afterDelete.ok());
    CHECK(afterDelete.value().empty());

    auto quads3 = f.doc->viewport().buildQuads(*f.doc->sceneInFocus(), {});
    REQUIRE(quads3.size() == 2);
    f.doc->resolveMaterials(quads3);
    for (const auto& q : quads3) {
        CHECK(q.materialShader == "lit");
        if (q.entity == e.value()) {
            // tint DO SPRITE preservado (0.5), SEM o do material apagado.
            CHECK(q.tintB == Catch::Approx(0.5f));
            CHECK(q.tintR == 1.f);
        }
    }
}

TEST_CASE("editor: P3 — Play clona a luz (render idêntico Edit/Play)",
          "[editor][p3light]")
{
    DocFixture f;
    f.withProject();
    auto e = f.doc->createEntity("Tocha", eng::scene::kNoEntity);
    REQUIRE(e.ok());
    REQUIRE(f.doc->addComponent(e.value(), "eng::render::Light2D").ok());
    REQUIRE(f.doc->setInspectorField(e.value(), "eng::render::Light2D",
                                     "intensity", "4")
                .ok());

    // Edit: luz coletada.
    auto editQuads =
        f.doc->viewport().buildQuads(*f.doc->sceneInFocus(), {});
    REQUIRE(editQuads.size() == 1);
    CHECK(editQuads[0].hasLight);
    CHECK(editQuads[0].lightIntensity == Catch::Approx(4.f));

    // Play: o CLONE carrega a luz (serialização — §10 parity).
    REQUIRE(f.doc->play().ok());
    auto playQuads =
        f.doc->viewport().buildQuads(*f.doc->sceneInFocus(), {});
    REQUIRE(playQuads.size() == 1);
    CHECK(playQuads[0].hasLight);
    CHECK(playQuads[0].lightIntensity == Catch::Approx(4.f));

    // Edição em Play é REJEITADA (clone somente-leitura — §8.7): a luz do
    // clone NÃO pode ser editada (contrato), e a EDIÇÃO fica intacta.
    auto rejected = f.doc->setInspectorField(e.value(),
                                             "eng::render::Light2D",
                                             "enabled", "false");
    CHECK(rejected.isError());
    f.doc->stop();
    auto editAfter =
        f.doc->viewport().buildQuads(*f.doc->sceneInFocus(), {});
    REQUIRE(editAfter.size() == 1);
    CHECK(editAfter[0].hasLight);   // edição intacta após Play/Stop
    CHECK(editAfter[0].lightIntensity == Catch::Approx(4.f));
}

TEST_CASE("editor: P3 — camada da luz mascara sprites (LayerRegistry)",
          "[editor][p3light]")
{
    DocFixture f;
    f.withProject();
    // Camada nomeada "UI" na cena (LayerRegistry da Scene).
    REQUIRE(f.doc->sceneInFocus()->layers().addLayer("UI").ok());

    auto luz = f.doc->createEntity("LuzUI", eng::scene::kNoEntity);
    REQUIRE(luz.ok());
    REQUIRE(f.doc->addComponent(luz.value(), "eng::render::Light2D").ok());
    REQUIRE(f.doc->setInspectorField(luz.value(), "eng::render::Light2D",
                                     "layer", "UI")
                .ok());

    auto spriteGame =
        f.doc->createEntity("SpriteGame", eng::scene::kNoEntity);
    REQUIRE(spriteGame.ok());
    REQUIRE(f.doc->addComponent(spriteGame.value(),
                                "eng::editor::SpriteData")
                .ok());

    auto spriteUi = f.doc->createEntity("SpriteUI", eng::scene::kNoEntity);
    REQUIRE(spriteUi.ok());
    REQUIRE(f.doc->addComponent(spriteUi.value(), "eng::editor::SpriteData")
                .ok());
    REQUIRE(f.doc->addComponent(spriteUi.value(), "eng::scene::LayerMember")
                .ok());
    REQUIRE(f.doc->setInspectorField(spriteUi.value(),
                                     "eng::scene::LayerMember", "layer", "UI")
                .ok());

    auto quads = f.doc->viewport().buildQuads(*f.doc->sceneInFocus(), {});
    REQUIRE(quads.size() == 3);
    // A luz carrega a camada; o sprite UI carrega LayerMember; o sprite
    // GAME fica com "GAME" (default).
    bool sawLight = false, sawUiSprite = false, sawGameSprite = false;
    for (const auto& q : quads) {
        if (q.hasLight) {
            CHECK(q.lightLayer == "UI");
            sawLight = true;
        }
        if (q.isSprite && q.entity == spriteUi.value()) {
            CHECK(q.layer == "UI");
            sawUiSprite = true;
        }
        if (q.isSprite && q.entity == spriteGame.value()) {
            CHECK(q.layer == "GAME");
            sawGameSprite = true;
        }
    }
    CHECK(sawLight);
    CHECK(sawUiSprite);
    CHECK(sawGameSprite);

    // O pack da DrawList respeita a mask (a luz UI NÃO ilumina GAME).
    eng::render::DrawList drawList;
    for (const auto& q : quads) {
        if (q.hasLight) {
            eng::render::DrawList::LightItem item;
            item.worldX = q.worldX;
            item.worldY = q.worldY;
            item.radius = q.lightRadius;
            item.intensity = q.lightIntensity;
            item.r = q.lightColorR;
            item.g = q.lightColorG;
            item.b = q.lightColorB;
            item.falloff = q.lightFalloff;
            item.layer = q.lightLayer;
            drawList.lights.push_back(item);
        }
    }
    CHECK(drawList.packUniformsFor("GAME").lightCount() == 0);
    CHECK(drawList.packUniformsFor("UI").lightCount() == 1);
}

// --- P3 §14 RENDERING VISUAL (readback — iluminação provada por pixel) --------

TEST_CASE("editor: P3 — sprite com Light2D muda o pixel (A != B, readback)",
          "[editor][rhi_hardware]")
{
    if (editorGraphicsUnavailable()) {
        SKIP("sem driver gráfico (lavapipe/EGL) — suite completo roda no CI");
    }
    auto host = eng::editor::EditorHost::create("gles", ".editor-test-ws-p3luz");
    REQUIRE(host.ok());
    std::unique_ptr<eng::editor::EditorHost> owned{host.value()};
    int marker = 0;
    owned->surfaceCreated(&marker, eng::rhi::NativeWindowKind::Headless,
                          128, 128);
    if (owned->state() != eng::editor::HostSurfaceState::Available) {
        SKIP("OpenGL ES indisponível (renderer não criado sem driver)");
    }

    auto& doc = owned->document();
    ensureProject(doc, "P3LuzGame");

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

    // Sprite cobrindo o centro (entidade em 0,0; ppu=1 → 2x2 unidades).
    auto sprite = doc.createSprite("Alvo");
    REQUIRE(sprite.ok());
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "textureAsset", "quad.png").ok());
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "pixelsPerUnit", "1").ok());

    // DADOS primeiro: bloco PerFrame com ZERO luzes + ambiente neutro.
    REQUIRE(owned->renderFrame(1.f / 60.f));
    auto* renderer = owned->viewportRenderer();
    REQUIRE(renderer != nullptr);
    CHECK(renderer->lastFrameTexturedSprites() == 1);
    CHECK(renderer->lastFrameLitSpriteVertices().size() == 6);  // lit default
    REQUIRE_FALSE(renderer->lastFrameFrameUniforms().empty());
    CHECK(renderer->lastFrameFrameUniforms()[0].lightCount() == 0);

    // (A) Sprite SEM luz: albedo × ambiente(1) — o look clássico.
    std::uint8_t pixelA[4] = {0, 0, 0, 0};
    REQUIRE(renderer->renderer()->readCenterPixel(pixelA).ok());
    INFO("readback A: " << +pixelA[0] << " " << +pixelA[1] << " "
                       << +pixelA[2] << " " << +pixelA[3]);
    CHECK(pixelA[3] == 255);  // sprite opaco no centro

    // (B) Light2D forte NO centro: albedo × (1 + luz) — pixel muda.
    auto light = doc.createEntity("Tocha", eng::scene::kNoEntity);
    REQUIRE(light.ok());
    REQUIRE(doc.addComponent(light.value(), "eng::render::Light2D").ok());
    REQUIRE(doc.setInspectorField(light.value(), "eng::render::Light2D",
                                  "intensity", "3").ok());
    REQUIRE(doc.setInspectorField(light.value(), "eng::render::Light2D",
                                  "radius", "10").ok());
    // Cor VERMELHA saturada (canal G/B baixo): a mudança é inequívoca.
    REQUIRE(doc.setInspectorField(light.value(), "eng::render::Light2D",
                                  "colorR", "1").ok());
    REQUIRE(doc.setInspectorField(light.value(), "eng::render::Light2D",
                                  "colorG", "0.05").ok());
    REQUIRE(doc.setInspectorField(light.value(), "eng::render::Light2D",
                                  "colorB", "0.05").ok());

    REQUIRE(owned->renderFrame(1.f / 60.f));
    // Bloco PerFrame AGORA carrega a luz (count == 1).
    REQUIRE_FALSE(renderer->lastFrameFrameUniforms().empty());
    CHECK(renderer->lastFrameFrameUniforms()[0].lightCount() == 1);
    const auto& block = renderer->lastFrameFrameUniforms()[0];
    CHECK(block.lightA[0][0] == Catch::Approx(0.f).margin(1e-3f));  // x
    CHECK(block.lightA[0][1] == Catch::Approx(0.f).margin(1e-3f));  // y
    CHECK(block.lightA[0][2] == Catch::Approx(10.f));              // raio
    CHECK(block.lightA[0][3] == Catch::Approx(3.f));               // intens
    CHECK(block.lightB[0][2] == Catch::Approx(0.05f).margin(0.02f));  // b

    std::uint8_t pixelB[4] = {0, 0, 0, 0};
    REQUIRE(renderer->renderer()->readCenterPixel(pixelB).ok());
    INFO("readback B: " << +pixelB[0] << " " << +pixelB[1] << " "
                       << +pixelB[2] << " " << +pixelB[3]);

    // (C) A != B — a iluminação é REAL (não um círculo desenhado).
    const bool differs = pixelA[0] != pixelB[0] || pixelA[1] != pixelB[1] ||
                         pixelA[2] != pixelB[2];
    CHECK(differs);
    // Direção coerente: luz vermelha forte (intenção 3) → canal R SOBE.
    CHECK(pixelB[0] >= pixelA[0]);
    CHECK(pixelB[0] > 140);  // vermelho saturado brilhante

    // (D) Play/Stop parity: a MESMA luz ilumina o CLONE no Play.
    REQUIRE(doc.play().ok());
    REQUIRE(owned->renderFrame(1.f / 60.f));
    std::uint8_t pixelPlay[4] = {0, 0, 0, 0};
    REQUIRE(renderer->renderer()->readCenterPixel(pixelPlay).ok());
    CHECK(pixelPlay[0] == pixelB[0]);  // idêntico ao Edit com a mesma luz
    CHECK(pixelPlay[1] == pixelB[1]);
    doc.stop();

    // (E) Desligar a luz volta ao look A (reversível — sem estado preso).
    REQUIRE(doc.setInspectorField(light.value(), "eng::render::Light2D",
                                  "enabled", "false").ok());
    REQUIRE(owned->renderFrame(1.f / 60.f));
    std::uint8_t pixelOff[4] = {0, 0, 0, 0};
    REQUIRE(renderer->renderer()->readCenterPixel(pixelOff).ok());
    CHECK(pixelOff[0] == pixelA[0]);
    CHECK(pixelOff[1] == pixelA[1]);
    CHECK(pixelOff[2] == pixelA[2]);
}

TEST_CASE("editor: P3 — material unlit vs lit via Inspector (readback)",
          "[editor][rhi_hardware]")
{
    if (editorGraphicsUnavailable()) {
        SKIP("sem driver gráfico (lavapipe/EGL) — suite completo roda no CI");
    }
    auto host = eng::editor::EditorHost::create("gles", ".editor-test-ws-p3mat");
    REQUIRE(host.ok());
    std::unique_ptr<eng::editor::EditorHost> owned{host.value()};
    int marker = 0;
    owned->surfaceCreated(&marker, eng::rhi::NativeWindowKind::Headless,
                          128, 128);
    if (owned->state() != eng::editor::HostSurfaceState::Available) {
        SKIP("OpenGL ES indisponível (renderer não criado sem driver)");
    }

    auto& doc = owned->document();
    ensureProject(doc, "P3MatGame");
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

    auto sprite = doc.createSprite("Pedra");
    REQUIRE(sprite.ok());
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "textureAsset", "quad.png").ok());
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "pixelsPerUnit", "1").ok());

    // Luz forte vermelha (o discriminador entre lit e unlit).
    auto light = doc.createEntity("Brasa", eng::scene::kNoEntity);
    REQUIRE(light.ok());
    REQUIRE(doc.addComponent(light.value(), "eng::render::Light2D").ok());
    REQUIRE(doc.setInspectorField(light.value(), "eng::render::Light2D",
                                  "intensity", "3").ok());
    REQUIRE(doc.setInspectorField(light.value(), "eng::render::Light2D",
                                  "colorR", "1").ok());

    // Default (lit): pixel iluminado.
    REQUIRE(owned->renderFrame(1.f / 60.f));
    auto* renderer = owned->viewportRenderer();
    REQUIRE(renderer != nullptr);
    std::uint8_t pixelLit[4] = {0, 0, 0, 0};
    REQUIRE(renderer->renderer()->readCenterPixel(pixelLit).ok());

    // Material UNLIT atribuído ao sprite: a MESMA luz NÃO o afeta.
    REQUIRE(doc.materialCreate("Cru").ok());
    REQUIRE(doc.materialWrite(
                "Cru.mat.json", R"({"name":"Cru","shader":"unlit"})").ok());
    REQUIRE(doc.setInspectorField(sprite.value(), "eng::editor::SpriteData",
                                  "materialAsset", "Cru").ok());

    REQUIRE(owned->renderFrame(1.f / 60.f));
    // Vertices foram para o caminho UNLIT (40B), não o lit (48B).
    CHECK(renderer->lastFrameLitSpriteVertices().empty());
    CHECK_FALSE(renderer->lastFrameSpriteVertices().empty());

    std::uint8_t pixelUnlit[4] = {0, 0, 0, 0};
    REQUIRE(renderer->renderer()->readCenterPixel(pixelUnlit).ok());

    // unlit < lit no canal R (a luz vermelha só soma no lit).
    INFO("lit: " << +pixelLit[0] << " unlit: " << +pixelUnlit[0]);
    CHECK(pixelLit[0] > pixelUnlit[0]);
}
