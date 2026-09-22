// P4.7.0 Bloco 1 — ComponentContract v2 + hooks + categorias + event bus.
//
// Contratos são APLICAÇÃO DE AUTORIA: add/remove do Inspector recusam com
// erro PRECISO; hooks nativos (luz casa com camada, validação de
// geometria) rodam pelo MESMO caminho que a UI usa. O catálogo único
// (ADR-043) alimenta Inspector, NI-Script e estas provas.

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "eng/editor/EditorDocument.hpp"
#include "eng/editor/Inspector.hpp"
#include "eng/fs/MemoryFileSystem.hpp"
#include "eng/physics/Physics.hpp"
#include "eng/reflect/Reflect.hpp"
#include "eng/scene/SceneEvents.hpp"
#include "eng/scene/SceneSerializer.hpp"

namespace {

using namespace eng::editor;  // EditorDocument/Inspector sem qualificação

// Fixture local (espelha a DocFixture do EditorTests.cpp — TUs separados).
struct ContractFixture {
    eng::fs::MemoryFileSystem fsStorage;
    eng::fs::MemoryFileSystem* fs = &fsStorage;
    std::unique_ptr<EditorDocument> doc;
    ContractFixture()
    {
        auto created = EditorDocument::create(fsStorage, eng::fs::Path{"."});
        REQUIRE(created.ok());
        doc = std::move(created.value());
    }

    void withProject() { REQUIRE(doc->newProject("ContractGame").ok()); }
};

// Componente de PROVA (single=true) — registrado apenas neste TU.
struct P47SingleProbe {
    float value = 0.f;
};

ENG_REFLECT_BEGIN(P47SingleProbe)
ENG_REFLECT_FIELD(value)
ENG_REFLECT_END()

const bool p47_single_probe_registered = [] {
    eng::scene::detail::ComponentContract contract;
    contract.single = true;
    contract.category = "Lógica";
    contract.scriptAlias = "probe";
    // Nome = o do reflect (ENG_REFLECT_BEGIN usa o token literal).
    (void)eng::scene::SceneSerializer::registerComponentType<P47SingleProbe>(
        "P47SingleProbe", std::move(contract));
    return true;
}();

} // namespace

// =============================================================================
// Contratos no add (requires / conflicts / single)
// =============================================================================

TEST_CASE("p47: contrato — CharacterBody exige Collider (add recusa com erro)",
          "[editor][p47]")
{
    ContractFixture f;
    f.withProject();

    auto entity = f.doc->createEntity("Heroi", eng::scene::kNoEntity);
    REQUIRE(entity.ok());

    // Sem Collider: recusa com o nome do requisito na mensagem.
    auto denied = f.doc->addComponent(entity.value(),
                                      "eng::physics::CharacterBody");
    REQUIRE(denied.isError());
    INFO(denied.error().message);
    CHECK(denied.error().message.find("eng::physics::Collider")
          != std::string::npos);
    CHECK(denied.error().message.find("exige") != std::string::npos);
    // E NADA foi anexado (o add é atômico — sem estado parcial).
    auto present = eng::editor::Inspector::componentsOf(
        *f.doc->sceneInFocus(), entity.value());
    CHECK(std::find(present.begin(), present.end(),
                    "eng::physics::CharacterBody") == present.end());

    // Collider primeiro → CharacterBody entra.
    REQUIRE(f.doc->addComponent(entity.value(), "eng::physics::Collider").ok());
    REQUIRE(f.doc->addComponent(entity.value(),
                                "eng::physics::CharacterBody")
                .ok());
}

TEST_CASE("p47: contrato — conflito RigidBody × CharacterBody é bidirecional",
          "[editor][p47]")
{
    ContractFixture f;
    f.withProject();

    auto entity = f.doc->createEntity("Corpo", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(f.doc->addComponent(entity.value(), "eng::physics::Collider").ok());
    REQUIRE(f.doc->addComponent(entity.value(),
                                "eng::physics::CharacterBody")
                .ok());

    // CharacterBody presente → RigidBody recusado (conflito).
    auto deniedRb = f.doc->addComponent(entity.value(),
                                        "eng::physics::RigidBody");
    REQUIRE(deniedRb.isError());
    INFO(deniedRb.error().message);
    CHECK(deniedRb.error().message.find("conflita") != std::string::npos);

    // E no outro sentido: nó novo com RigidBody recusa CharacterBody.
    auto entity2 = f.doc->createEntity("Corpo2", eng::scene::kNoEntity);
    REQUIRE(entity2.ok());
    REQUIRE(f.doc->addComponent(entity2.value(), "eng::physics::Collider").ok());
    REQUIRE(f.doc->addComponent(entity2.value(),
                                "eng::physics::RigidBody")
                .ok());
    auto deniedCb = f.doc->addComponent(entity2.value(),
                                        "eng::physics::CharacterBody");
    REQUIRE(deniedCb.isError());
    CHECK(deniedCb.error().message.find("conflita") != std::string::npos);
}

TEST_CASE("p47: single — segunda instância na cena é recusada", "[editor][p47]")
{
    ContractFixture f;
    f.withProject();

    auto a = f.doc->createEntity("A", eng::scene::kNoEntity);
    auto b = f.doc->createEntity("B", eng::scene::kNoEntity);
    REQUIRE(a.ok());
    REQUIRE(b.ok());

    const std::string probe = "P47SingleProbe";
    REQUIRE(f.doc->addComponent(a.value(), probe).ok());

    auto denied = f.doc->addComponent(b.value(), probe);
    REQUIRE(denied.isError());
    INFO(denied.error().message);
    CHECK(denied.error().message.find("único na cena") != std::string::npos);

    // Removido o único, o add volta a ser possível.
    REQUIRE(f.doc->removeComponent(a.value(), probe).ok());
    REQUIRE(f.doc->addComponent(b.value(), probe).ok());
}

TEST_CASE("p47: remoção com DEPENDENTE recusa e nomeia quem exige",
          "[editor][p47]")
{
    ContractFixture f;
    f.withProject();

    auto entity = f.doc->createEntity("Corpo", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(f.doc->addComponent(entity.value(), "eng::physics::Collider").ok());
    REQUIRE(f.doc->addComponent(entity.value(),
                                "eng::physics::CharacterBody")
                .ok());

    // Collider sustenta o CharacterBody: remoção recusa com o dependente.
    auto denied = f.doc->removeComponent(entity.value(),
                                         "eng::physics::Collider");
    REQUIRE(denied.isError());
    INFO(denied.error().message);
    CHECK(denied.error().message.find("CharacterBody") != std::string::npos);
    CHECK(denied.error().message.find("exige") != std::string::npos);
    // O Collider CONTINUA presente (nada sumiu de surpresa).
    auto present = eng::editor::Inspector::componentsOf(
        *f.doc->sceneInFocus(), entity.value());
    CHECK(std::find(present.begin(), present.end(),
                    "eng::physics::Collider") != present.end());

    // Ordem certa: dependente primeiro, dependência depois.
    REQUIRE(f.doc->removeComponent(entity.value(),
                                   "eng::physics::CharacterBody")
                .ok());
    REQUIRE(f.doc->removeComponent(entity.value(),
                                   "eng::physics::Collider")
                .ok());
}

// =============================================================================
// Contrato → UI: categorias (ordem fixa) e hints de dependência
// =============================================================================

TEST_CASE("p47: catalogEntries — categorias do contrato em ordem fixa",
          "[editor][p47]")
{
    const auto entries = eng::editor::Inspector::catalogEntries();
    REQUIRE_FALSE(entries.empty());

    // Ordem de categoria NUNCA decresce (Transform → Render → Física → …).
    auto orderOf = [](const std::string& category) {
        static const std::vector<std::string> kOrder = {
            "Transform", "Render", "Física", "Lógica",
            "Áudio", "Câmera", "FX", "Outros",
        };
        return static_cast<std::size_t>(
            std::find(kOrder.begin(), kOrder.end(), category)
            - kOrder.begin());
    };
    for (std::size_t i = 1; i < entries.size(); ++i) {
        CAPTURE(entries[i - 1].name, entries[i].name);
        CHECK(orderOf(entries[i - 1].category)
              <= orderOf(entries[i].category));
    }

    // Categorias esperadas por componente (fonte = contrato registrado).
    auto categoryOf = [&](const std::string& name) -> std::string {
        for (const auto& entry : entries) {
            if (entry.name == name) {
                return entry.category;
            }
        }
        return "";
    };
    CHECK(categoryOf("eng::editor::SpriteData") == "Render");
    CHECK(categoryOf("eng::physics::RigidBody") == "Física");
    CHECK(categoryOf("eng::editor::NiScriptComponent") == "Lógica");
    CHECK(categoryOf("eng::editor::AudioSource") == "Áudio");
    CHECK(categoryOf("eng::tick::CameraData") == "Câmera");
    CHECK(categoryOf("eng::render::Light2D") == "FX");
    CHECK(categoryOf("eng::particles::ParticleEmitter") == "FX");
    CHECK(categoryOf("eng::math::Transform") == "Transform");
}

TEST_CASE("p47: hint de dependência vem do CONTRATO (fonte única)",
          "[editor][p47]")
{
    ContractFixture f;
    f.withProject();
    auto entity = f.doc->createEntity("X", eng::scene::kNoEntity);
    REQUIRE(entity.ok());

    auto addable = f.doc->addableComponents(entity.value());
    auto hintOf = [&](const std::string& name) -> std::string {
        for (const auto& item : addable) {
            if (item.name == name) {
                return item.dependency;
            }
        }
        return "";
    };
    const std::string hint = hintOf("eng::physics::CharacterBody");
    CHECK(hint.find("eng::physics::Collider") != std::string::npos);
}

// =============================================================================
// Hooks: onValidate (geometria do Collider) + onAttach (luz)
// =============================================================================

TEST_CASE("p47: onValidate — radius negativo rejeitado COM rollback",
          "[editor][p47]")
{
    ContractFixture f;
    f.withProject();

    auto entity = f.doc->createEntity("Caixa", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(f.doc->addComponent(entity.value(), "eng::physics::Collider").ok());

    // Escrita válida: entra.
    REQUIRE(f.doc
                ->setInspectorField(entity.value(), "eng::physics::Collider",
                                    "radius", "2")
                .ok());

    // Escrita INVÁLIDA: rejeitada com erro preciso e ROLLBACK pro valor
    // anterior (2) — o componente nunca fica num estado quebrado.
    auto denied = f.doc->setInspectorField(
        entity.value(), "eng::physics::Collider", "radius", "-1");
    REQUIRE(denied.isError());
    INFO(denied.error().message);
    CHECK(denied.error().message.find("radius") != std::string::npos);
    auto radius = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), entity.value(), "eng::physics::Collider",
        "radius");
    REQUIRE(radius.ok());
    CHECK(radius.value() == "2");
}

TEST_CASE("p47: onAttach da luz casa com a camada via caminho do Inspector",
          "[editor][p47]")
{
    ContractFixture f;
    f.withProject();

    // Sprite em camada "UI" (o hook conta sprites LIT por camada).
    auto sprite = f.doc->createEntity("Sprite", eng::scene::kNoEntity);
    REQUIRE(sprite.ok());
    REQUIRE(f.doc->addComponent(sprite.value(), "eng::editor::SpriteData").ok());
    REQUIRE(f.doc->addComponent(sprite.value(), "eng::scene::LayerMember").ok());
    REQUIRE(f.doc
                ->setInspectorField(sprite.value(), "eng::scene::LayerMember",
                                    "layer", "UI")
                .ok());

    // Luz anexada pelo MESMO caminho da UI (EditorDocument::addComponent →
    // hook onAttach) herda a camada dominante.
    auto light = f.doc->createEntity("Luz", eng::scene::kNoEntity);
    REQUIRE(light.ok());
    REQUIRE(f.doc->addComponent(light.value(), "eng::render::Light2D").ok());
    auto layer = eng::editor::Inspector::getField(
        *f.doc->sceneInFocus(), light.value(), "eng::render::Light2D", "layer");
    REQUIRE(layer.ok());
    CHECK(layer.value() == "UI");
}

// =============================================================================
// Play: validação de contratos é a última linha de defesa
// =============================================================================

TEST_CASE("p47: play() recusa cena com Collider inválido (erro com nó)",
          "[editor][p47]")
{
    ContractFixture f;
    f.withProject();

    auto entity = f.doc->createEntity("Quebrado", eng::scene::kNoEntity);
    REQUIRE(entity.ok());
    REQUIRE(f.doc->addComponent(entity.value(), "eng::physics::Collider").ok());

    // Corrompe POR BAIXO do Inspector (caminho externo — hand-edit/bug).
    auto* collider =
        f.doc->sceneInFocus()->world().get<eng::physics::Collider>(
            entity.value());
    REQUIRE(collider != nullptr);
    collider->radius = -0.5f;

    auto started = f.doc->play();
    REQUIRE(started.isError());
    INFO(started.error().message);
    CHECK(started.error().message.find("eng::physics::Collider")
          != std::string::npos);
    CHECK(started.error().message.find("Play") != std::string::npos);
    f.doc->stop();
}

// =============================================================================
// Bridge NI-Script: on_hit do script roda quando a física publica
// =============================================================================

TEST_CASE("p47: bridge NI-Script — up on_hit roda no self atingido",
          "[editor][p47][niscript]")
{
    ContractFixture f;
    f.withProject();

    // Jogador com Collider + script on_hit (move cru: 0,10 — teleporte
    // documentado; serve de FLAG observável do handler).
    auto player = f.doc->createEntity("Jogador", eng::scene::kNoEntity);
    REQUIRE(player.ok());
    REQUIRE(f.doc->addComponent(player.value(), "eng::physics::Collider").ok());
    REQUIRE(f.doc->addComponent(player.value(),
                                "eng::editor::NiScriptComponent")
                .ok());
    const char* source = "up on_hit:\n"
                         "    move(0, 10)\n"
                         "stop\n";
    REQUIRE(f.doc
                ->setInspectorField(player.value(),
                                    "eng::editor::NiScriptComponent",
                                    "source", source)
                .ok());

    // Parede sobreposta (dois colliders estáticos → contato → on_hit).
    auto wall = f.doc->createEntity("Parede", eng::scene::kNoEntity);
    REQUIRE(wall.ok());
    REQUIRE(f.doc->addComponent(wall.value(), "eng::physics::Collider").ok());
    REQUIRE(f.doc
                ->setInspectorField(wall.value(), "eng::math::Transform",
                                    "position.x", "0.5")
                .ok());

    REQUIRE(f.doc->play().ok());
    f.doc->tick(1.f / 60.f); // física publica; bridge roda on_hit

    // Leitura pelo leitor TRADUZIDO do documento (edit→runtime —
    // inspectorFields aplica toFocus internamente).
    const auto fields = f.doc->inspectorFields(player.value(),
                                               "eng::math::Transform");
    float py = 0.f;
    bool foundY = false;
    for (const auto& field : fields) {
        if (field.path == "position.y") {
            py = std::stof(field.value);
            foundY = true;
        }
    }
    REQUIRE(foundY);
    INFO("player.y = " << py);
    CHECK(py == Catch::Approx(10.f).margin(0.1f));
    f.doc->stop();
}

// =============================================================================
// Event bus da cena + eventos de física (on_hit / triggers)
// =============================================================================

TEST_CASE("p47: eventos de física — on_hit nos dois sentidos com normal oposta",
          "[physics][p47]")
{
    eng::scene::Scene scene;

    auto a = scene.createNode();
    auto b = scene.createNode();
    REQUIRE(scene.isNode(a));
    REQUIRE(scene.isNode(b));

    // Duas esferas sobrepostas (raio 0.5, centros a 0.5 de distância).
    REQUIRE(scene.world().emplace<eng::physics::Collider>(
        a, eng::physics::Collider{}) != nullptr);
    REQUIRE(scene.world().emplace<eng::physics::Collider>(
        b, eng::physics::Collider{}) != nullptr);
    auto* ta = scene.localTransform(a);
    auto* tb = scene.localTransform(b);
    REQUIRE(ta != nullptr);
    REQUIRE(tb != nullptr);
    ta->position = eng::math::Vec3{0.f, 0.f, 0.f};
    tb->position = eng::math::Vec3{0.5f, 0.f, 0.f};

    std::vector<eng::scene::HitEvent> hits;
    auto subscription = scene.events().subscribe<eng::scene::HitEvent>(
        [&](const eng::scene::HitEvent& event) { hits.push_back(event); });

    eng::physics::PhysicsWorld world;
    world.step(scene, 1.f / 60.f);

    // on_hit é publicado nos DOIS sentidos (self/other trocados).
    REQUIRE(hits.size() == 2);
    const bool ab = hits[0].self == a && hits[1].self == b;
    const bool ba = hits[0].self == b && hits[1].self == a;
    CHECK((ab || ba));
    // Normais opostas (mesma linha de contato).
    CHECK(hits[0].nx == Catch::Approx(-hits[1].nx).margin(1e-5f));
    CHECK(hits[0].ny == Catch::Approx(-hits[1].ny).margin(1e-5f));
}

TEST_CASE("p47: eventos de trigger — on_enter único, on_exit ao separar",
          "[physics][p47]")
{
    eng::scene::Scene scene;
    auto a = scene.createNode();
    auto b = scene.createNode();
    eng::physics::Collider triggerA{};
    triggerA.isTrigger = true;
    eng::physics::Collider triggerB{};
    triggerB.isTrigger = true;
    REQUIRE(scene.world().emplace<eng::physics::Collider>(a, triggerA)
            != nullptr);
    REQUIRE(scene.world().emplace<eng::physics::Collider>(b, triggerB)
            != nullptr);
    auto* ta = scene.localTransform(a);
    auto* tb = scene.localTransform(b);
    REQUIRE(ta != nullptr);
    REQUIRE(tb != nullptr);
    ta->position = eng::math::Vec3{0.f, 0.f, 0.f};
    tb->position = eng::math::Vec3{0.2f, 0.f, 0.f}; // sobrepostos

    int entered = 0;
    int exited = 0;
    auto subEnter = scene.events().subscribe<eng::scene::TriggerEvent>(
        [&](const eng::scene::TriggerEvent& event) {
            if (event.entered) {
                ++entered;
            } else {
                ++exited;
            }
        });

    eng::physics::PhysicsWorld world;
    world.step(scene, 1.f / 60.f);
    CHECK(entered == 2); // nos dois sentidos (A→B e B→A)
    CHECK(exited == 0);

    // Continuam sobrepostos: SEM novo on_enter (evento é transição).
    world.step(scene, 1.f / 60.f);
    CHECK(entered == 2);

    // Separa (além dos raios 0.5+0.5): on_exit nos dois sentidos.
    tb->position = eng::math::Vec3{3.f, 0.f, 0.f};
    world.step(scene, 1.f / 60.f);
    CHECK(entered == 2);
    CHECK(exited == 2);
}

TEST_CASE("p47: bus da cena — publish determinístico e contagem de inscritos",
          "[scene][p47]")
{
    eng::scene::Scene scene;
    CHECK(scene.events().subscriberCount<eng::scene::HitEvent>() == 0);

    int count = 0;
    {
        auto sub = scene.events().subscribe<eng::scene::HitEvent>(
            [&](const eng::scene::HitEvent&) { ++count; });
        CHECK(scene.events().subscriberCount<eng::scene::HitEvent>() == 1);
        eng::scene::HitEvent event;
        scene.events().publish(event);
        scene.events().publish(event);
        CHECK(count == 2);
        sub.unsubscribe();
    }
    CHECK(scene.events().subscriberCount<eng::scene::HitEvent>() == 0);
    eng::scene::HitEvent event;
    scene.events().publish(event); // sem inscritos: no-op, sem crash
    CHECK(count == 2);
}
