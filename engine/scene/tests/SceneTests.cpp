#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "eng/math/Quat.hpp"
#include "eng/math/Vec3.hpp"
#include "eng/math/Vec4.hpp"
#include "eng/scene/Scene.hpp"

namespace {

constexpr float kEps = 1e-4f;

Catch::Approx approx(float value)
{
    // epsilon (relativo) para magnitudes normais; margin (absoluto) para
    // comparações contra ~0, onde epsilon relativo nunca satisfaz.
    return Catch::Approx(value).epsilon(kEps).margin(kEps);
}

} // namespace

// =============================================================================
// Criação, anexação, hierarquia
// =============================================================================

TEST_CASE("scene: createNode devolve raiz com transform identidade", "[scene]")
{
    eng::scene::Scene scene;

    const auto node = scene.createNode();

    CHECK(scene.isNode(node));
    CHECK(scene.nodeCount() == 1);
    CHECK(scene.parentOf(node) == eng::scene::kNoEntity);
    CHECK(scene.childCount(node) == 0);

    const auto* local = scene.localTransform(node);
    REQUIRE(local != nullptr);
    CHECK(local->position == eng::math::Vec3{});
    CHECK(local->scale == eng::math::Vec3{1.0f, 1.0f, 1.0f});
}

TEST_CASE("scene: attach/detach mantêm pai, contagem e ordem", "[scene]")
{
    eng::scene::Scene scene;

    const auto parent = scene.createNode();
    const auto a = scene.createNode();
    const auto b = scene.createNode();
    const auto c = scene.createNode();

    CHECK(scene.attach(a, parent));
    CHECK(scene.attach(b, parent));
    CHECK(scene.attach(c, parent));

    CHECK(scene.parentOf(a) == parent);
    CHECK(scene.parentOf(c) == parent);
    CHECK(scene.childCount(parent) == 3);

    std::vector<eng::ecs::Entity> order;
    scene.eachChild(parent, [&](eng::ecs::Entity child) { order.push_back(child); });
    REQUIRE(order.size() == 3);
    CHECK(order[0] == a); // ordem de anexação
    CHECK(order[1] == b);
    CHECK(order[2] == c);

    CHECK(scene.detach(b));
    CHECK(scene.childCount(parent) == 2);
    CHECK(scene.parentOf(b) == eng::scene::kNoEntity); // virou raiz

    CHECK_FALSE(scene.detach(b));  // já raiz
    CHECK_FALSE(scene.detach(parent)); // raiz de fábrica
}

TEST_CASE("scene: attach no MESMO pai é idempotente", "[scene]")
{
    eng::scene::Scene scene;

    const auto parent = scene.createNode();
    const auto child = scene.createNode();

    CHECK(scene.attach(child, parent));
    CHECK(scene.attach(child, parent)); // idempotente
    CHECK(scene.childCount(parent) == 1); // não duplicou
}

TEST_CASE("scene: re-anexação MOVE o filho (pai antigo perde)", "[scene]")
{
    eng::scene::Scene scene;

    const auto pai1 = scene.createNode();
    const auto pai2 = scene.createNode();
    const auto child = scene.createNode();

    CHECK(scene.attach(child, pai1));
    CHECK(scene.attach(child, pai2)); // move

    CHECK(scene.childCount(pai1) == 0);
    CHECK(scene.childCount(pai2) == 1);
    CHECK(scene.parentOf(child) == pai2);

    std::vector<eng::ecs::Entity> filhos;
    scene.eachChild(pai2, [&](eng::ecs::Entity e) { filhos.push_back(e); });
    REQUIRE(filhos.size() == 1);
    CHECK(filhos[0] == child);
}

TEST_CASE("scene: attach rejeita inválidos, self e CICLOS", "[scene]")
{
    eng::scene::Scene scene;

    const auto root = scene.createNode();
    const auto mid = scene.createNode();
    const auto leaf = scene.createNode();

    CHECK(scene.attach(mid, root));
    CHECK(scene.attach(leaf, mid));
    // root → mid → leaf

    CHECK_FALSE(scene.attach(leaf, leaf));       // self
    CHECK_FALSE(scene.attach(root, leaf));       // ciclo: root sob seu descendente
    CHECK_FALSE(scene.attach(mid, leaf));        // ciclo: mid sob seu filho
    CHECK(scene.parentOf(mid) == root);          // árvore intacta
    CHECK(scene.parentOf(leaf) == mid);

    // handles obsoletos:
    const auto stale = scene.createNode();
    (void)scene.world().destroy(stale);
    CHECK_FALSE(scene.attach(stale, root));      // child obsoleto
    CHECK_FALSE(scene.attach(root, stale));      // parent obsoleto
    CHECK(scene.childCount(root) == 1);          // nada mudou
}

// =============================================================================
// Destruição em cascata
// =============================================================================

TEST_CASE("scene: destroyNode derruba a subárvore inteira", "[scene]")
{
    eng::scene::Scene scene;

    const auto root = scene.createNode();
    const auto a = scene.createNode();
    const auto b = scene.createNode();
    const auto a1 = scene.createNode();
    const auto a2 = scene.createNode();
    CHECK(scene.attach(a, root));
    CHECK(scene.attach(b, root));
    CHECK(scene.attach(a1, a));
    CHECK(scene.attach(a2, a));

    CHECK(scene.nodeCount() == 5);
    REQUIRE(scene.destroyNode(a));

    CHECK(scene.isNode(root));
    CHECK(scene.isNode(b));
    CHECK_FALSE(scene.isNode(a));
    CHECK_FALSE(scene.isNode(a1));
    CHECK_FALSE(scene.isNode(a2));
    CHECK(scene.nodeCount() == 2);
    CHECK(scene.childCount(root) == 1); // só b sobrou
}

TEST_CASE("scene: destroyNode desanexa do pai — pai e irmãos sobrevivem", "[scene]")
{
    eng::scene::Scene scene;

    const auto pai = scene.createNode();
    const auto filho = scene.createNode();
    const auto neto = scene.createNode();
    CHECK(scene.attach(filho, pai));
    CHECK(scene.attach(neto, filho));

    REQUIRE(scene.destroyNode(filho));

    CHECK(scene.isNode(pai));
    CHECK_FALSE(scene.isNode(filho));
    CHECK_FALSE(scene.isNode(neto));
    CHECK(scene.childCount(pai) == 0); // lista do pai limpa
}

TEST_CASE("scene: destroyNode com handle obsoleto é no-op", "[scene]")
{
    eng::scene::Scene scene;

    const auto node = scene.createNode();
    REQUIRE(scene.destroyNode(node));
    CHECK_FALSE(scene.destroyNode(node));
    CHECK(scene.nodeCount() == 0);
}

TEST_CASE("scene: destruição direta no world é tolerada (limpeza oportunista)", "[scene]")
{
    eng::scene::Scene scene;

    const auto pai = scene.createNode();
    const auto a = scene.createNode();
    const auto b = scene.createNode();
    CHECK(scene.attach(a, pai));
    CHECK(scene.attach(b, pai));

    (void)scene.world().destroy(a); // BYPASS da Scene — referência fica obsoleta

    CHECK(scene.childCount(pai) == 1); // obsoleta não conta
    std::vector<eng::ecs::Entity> vivos;
    scene.eachChild(pai, [&](eng::ecs::Entity child) { vivos.push_back(child); });
    REQUIRE(vivos.size() == 1);
    CHECK(vivos[0] == b);

    // detach de b limpa a lista do pai e a entrada obsoleta junto:
    CHECK(scene.detach(b));
    const auto* hierarchy = scene.world().get<eng::scene::Hierarchy>(pai);
    REQUIRE(hierarchy != nullptr);
    CHECK(hierarchy->children.empty()); // limpeza oportunista removeu a morta
}

// =============================================================================
// Transforms: local e mundo
// =============================================================================

TEST_CASE("scene: cadeia de translações compõe mundo correto", "[scene]")
{
    eng::scene::Scene scene;

    const auto pai = scene.createNode();
    const auto filho = scene.createNode();
    CHECK(scene.attach(filho, pai));

    scene.localTransform(pai)->position = {10.0f, 0.0f, 0.0f};
    scene.localTransform(filho)->position = {0.0f, 5.0f, 2.0f};

    const eng::math::Mat4 world = scene.computeWorldMatrix(filho);
    const eng::math::Vec3 pos = world.transformPoint({0.0f, 0.0f, 0.0f});
    CHECK(pos.x == approx(10.0f));
    CHECK(pos.y == approx(5.0f));
    CHECK(pos.z == approx(2.0f));

    // oráculo independente: transformPoint do pai sobre a posição local do filho
    const auto expected = scene.localTransform(pai)->transformPoint({0.0f, 5.0f, 2.0f});
    CHECK(pos.x == approx(expected.x));
    CHECK(pos.y == approx(expected.y));
    CHECK(pos.z == approx(expected.z));
}

TEST_CASE("scene: rotação do pai gira o offset do filho", "[scene]")
{
    eng::scene::Scene scene;

    const auto pai = scene.createNode();
    const auto filho = scene.createNode();
    CHECK(scene.attach(filho, pai));

    scene.localTransform(pai)->rotation =
        eng::math::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 1.5707964f); // 90° Y
    scene.localTransform(filho)->position = {1.0f, 0.0f, 0.0f};

    const eng::math::Vec3 pos =
        scene.computeWorldMatrix(filho).transformPoint({0.0f, 0.0f, 0.0f});
    const auto expected = scene.localTransform(pai)->transformPoint({1.0f, 0.0f, 0.0f});
    CHECK(pos.x == approx(expected.x));
    CHECK(pos.y == approx(expected.y));
    CHECK(pos.z == approx(expected.z));
    // convenção right-handed do motor: +X girado 90° em Y vai para -Z
    CHECK(pos.x == approx(0.0f));
    CHECK(pos.z == approx(-1.0f));
}

TEST_CASE("scene: escala do pai amplifica o offset do filho", "[scene]")
{
    eng::scene::Scene scene;

    const auto pai = scene.createNode();
    const auto filho = scene.createNode();
    CHECK(scene.attach(filho, pai));

    scene.localTransform(pai)->scale = {2.0f, 2.0f, 2.0f};
    scene.localTransform(filho)->position = {1.0f, 3.0f, -2.0f};

    const eng::math::Vec3 pos =
        scene.computeWorldMatrix(filho).transformPoint({0.0f, 0.0f, 0.0f});
    CHECK(pos.x == approx(2.0f));
    CHECK(pos.y == approx(6.0f));
    CHECK(pos.z == approx(-4.0f));
}

TEST_CASE("scene: cadeia de 3 níveis compõe em ordem", "[scene]")
{
    eng::scene::Scene scene;

    const auto a = scene.createNode();
    const auto b = scene.createNode();
    const auto c = scene.createNode();
    CHECK(scene.attach(b, a));
    CHECK(scene.attach(c, b));

    scene.localTransform(a)->position = {1.0f, 0.0f, 0.0f};
    scene.localTransform(b)->position = {0.0f, 2.0f, 0.0f};
    scene.localTransform(c)->position = {0.0f, 0.0f, 3.0f};

    const eng::math::Vec3 pos =
        scene.computeWorldMatrix(c).transformPoint({0.0f, 0.0f, 0.0f});
    CHECK(pos.x == approx(1.0f));
    CHECK(pos.y == approx(2.0f));
    CHECK(pos.z == approx(3.0f));

    scene.localTransform(b)->position = {0.0f, 20.0f, 0.0f}; // muda o meio
    const eng::math::Vec3 pos2 =
        scene.computeWorldMatrix(c).transformPoint({0.0f, 0.0f, 0.0f});
    CHECK(pos2.y == approx(20.0f)); // computeWorldMatrix é SEMPRE corrente
}

// =============================================================================
// updateWorldTransforms + cache
// =============================================================================

TEST_CASE("scene: updateWorldTransforms cacheia; worldMatrix lê o cache", "[scene]")
{
    eng::scene::Scene scene;

    const auto pai = scene.createNode();
    const auto filho = scene.createNode();
    CHECK(scene.attach(filho, pai));
    scene.localTransform(pai)->position = {4.0f, 0.0f, 0.0f};
    scene.localTransform(filho)->position = {0.0f, 0.0f, 7.0f};

    CHECK(scene.worldMatrix(filho) == nullptr); // ainda sem update

    scene.updateWorldTransforms();

    const auto* cached = scene.worldMatrix(filho);
    REQUIRE(cached != nullptr);
    const eng::math::Vec3 pos = cached->transformPoint({0.0f, 0.0f, 0.0f});
    CHECK(pos.x == approx(4.0f));
    CHECK(pos.z == approx(7.0f));

    // cache e computação fresca coincidem APÓS o update:
    const eng::math::Mat4 fresh = scene.computeWorldMatrix(filho);
    for (int i = 0; i < 16; ++i) {
        CHECK(fresh.m[i] == approx(cached->m[i]));
    }

    // nó criado DEPOIS do update: sem cache até o próximo update
    const auto novo = scene.createNode();
    CHECK(scene.worldMatrix(novo) == nullptr);
    scene.updateWorldTransforms();
    CHECK(scene.worldMatrix(novo) != nullptr);
}

TEST_CASE("scene: cache fica obsoleto sem update (documentado)", "[scene]")
{
    eng::scene::Scene scene;

    const auto node = scene.createNode();
    scene.localTransform(node)->position = {1.0f, 0.0f, 0.0f};
    scene.updateWorldTransforms();

    scene.localTransform(node)->position = {9.0f, 0.0f, 0.0f};
    // SEM update: cache antigo...
    const auto* cached = scene.worldMatrix(node);
    REQUIRE(cached != nullptr);
    CHECK(cached->transformPoint({0.0f, 0.0f, 0.0f}).x == approx(1.0f));
    // ...e computeWorldMatrix sempre corrente:
    CHECK(scene.computeWorldMatrix(node).transformPoint({0.0f, 0.0f, 0.0f}).x
          == approx(9.0f));

    scene.updateWorldTransforms();
    CHECK(scene.worldMatrix(node)->transformPoint({0.0f, 0.0f, 0.0f}).x == approx(9.0f));
}

TEST_CASE("scene: update cobre múltiplas raízes e filhos de obsoletos", "[scene]")
{
    eng::scene::Scene scene;

    const auto r1 = scene.createNode();
    const auto r2 = scene.createNode();
    const auto f1 = scene.createNode();
    const auto f2 = scene.createNode();
    CHECK(scene.attach(f1, r1));
    CHECK(scene.attach(f2, r2));

    scene.localTransform(r1)->position = {1.0f, 0.0f, 0.0f};
    scene.localTransform(r2)->position = {2.0f, 0.0f, 0.0f};
    scene.localTransform(f1)->position = {0.0f, 10.0f, 0.0f};
    scene.localTransform(f2)->position = {0.0f, 20.0f, 0.0f};

    scene.updateWorldTransforms();

    CHECK(scene.worldMatrix(f1)->transformPoint({0.0f, 0.0f, 0.0f}).y == approx(10.0f));
    CHECK(scene.worldMatrix(f2)->transformPoint({0.0f, 0.0f, 0.0f}).y == approx(20.0f));
    CHECK(scene.worldMatrix(f1)->transformPoint({0.0f, 0.0f, 0.0f}).x == approx(1.0f));
    CHECK(scene.worldMatrix(f2)->transformPoint({0.0f, 0.0f, 0.0f}).x == approx(2.0f));

    // nó com pai destruído por bypass: tratado como RAIZ no update
    const auto orfao = scene.createNode();
    CHECK(scene.attach(orfao, r2));
    (void)scene.world().destroy(r2); // bypass: órfão fica com pai obsoleto
    scene.localTransform(orfao)->position = {5.0f, 0.0f, 0.0f};

    scene.updateWorldTransforms();
    CHECK(scene.worldMatrix(orfao)->transformPoint({0.0f, 0.0f, 0.0f}).x == approx(5.0f));
}

// =============================================================================
// Iteração segura e profundidade
// =============================================================================

TEST_CASE("scene: eachChild com snapshot — mutar durante iteração é seguro", "[scene]")
{
    eng::scene::Scene scene;

    const auto pai = scene.createNode();
    const auto a = scene.createNode();
    const auto b = scene.createNode();
    const auto c = scene.createNode();
    CHECK(scene.attach(a, pai));
    CHECK(scene.attach(b, pai));
    CHECK(scene.attach(c, pai));

    std::size_t visited = 0;
    scene.eachChild(pai, [&](eng::ecs::Entity child) {
        ++visited;
        (void)scene.detach(child); // desanexa TODOS durante a iteração
    });

    CHECK(visited == 3);
    CHECK(scene.childCount(pai) == 0);
    CHECK(scene.parentOf(b) == eng::scene::kNoEntity);
}

TEST_CASE("scene: cadeia profunda (2000 níveis) — update iterativo sem estourar pilha", "[scene]")
{
    eng::scene::Scene scene;

    eng::ecs::Entity previous = scene.createNode();
    for (int i = 0; i < 1999; ++i) {
        const auto node = scene.createNode();
        CHECK(scene.attach(node, previous));
        previous = node;
    }
    CHECK(scene.nodeCount() == 2000);

    // acumula 2000 × 0.01 no X subindo a cadeia: folha em x=19.99
    scene.world().each<eng::math::Transform>(
        [](eng::ecs::Entity, eng::math::Transform& t) { t.position.x = 0.01f; });

    scene.updateWorldTransforms(); // iterativo — sem recursão

    const auto* leaf = scene.worldMatrix(previous);
    REQUIRE(leaf != nullptr);
    CHECK(leaf->transformPoint({0.0f, 0.0f, 0.0f}).x == approx(20.0f));
}

TEST_CASE("scene: const Scene expõe leitura", "[scene]")
{
    eng::scene::Scene scene;

    const auto node = scene.createNode();
    scene.localTransform(node)->position = {3.0f, 0.0f, 0.0f};
    scene.updateWorldTransforms();

    const eng::scene::Scene& constScene = scene;
    CHECK(constScene.isNode(node));
    CHECK(constScene.nodeCount() == 1);
    const auto* local = constScene.localTransform(node);
    REQUIRE(local != nullptr);
    CHECK(local->position.x == approx(3.0f));
    const auto* cached = constScene.worldMatrix(node);
    REQUIRE(cached != nullptr);
    CHECK(cached->transformPoint({0.0f, 0.0f, 0.0f}).x == approx(3.0f));
    CHECK(constScene.parentOf(node) == eng::scene::kNoEntity);
    CHECK(constScene.childCount(node) == 0);
}
