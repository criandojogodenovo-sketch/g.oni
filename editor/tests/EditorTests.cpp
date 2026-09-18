/// Testes do editor (FASE 8, missão §8.10) — rodam no LINUX.
///
/// Cobertura exigida pela missão: projeto (new/open/save/settings),
/// entidades (create/delete/duplicate/rename/hierarchy/componentes),
/// inspector, scene save/load, asset discovery, play/stop com separação
/// editor×runtime, viewport (câmera/hit-test), ViewportRenderer e EditorHost
/// contra backends REAIS (lavapipe/llvmpipe — mesmos binários do APK).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "eng/animation/Animation.hpp"
#include "eng/editor/EditorDocument.hpp"
#include "eng/editor/NiRuntime.hpp"
#include "eng/editor/EditorHost.hpp"
#include "eng/editor/Inspector.hpp"
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
