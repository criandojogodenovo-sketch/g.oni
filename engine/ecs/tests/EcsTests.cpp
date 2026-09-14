#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "eng/ecs/Ecs.hpp"

namespace {

struct Position {
    float x = 0.0f;
    float y = 0.0f;
};

struct Velocity {
    float dx = 0.0f;
    float dy = 0.0f;
};

struct Tag {
    std::string label;
};

/// Componente move-only (exige move-construct, não copy): valida swap-and-pop.
struct Owner {
    std::unique_ptr<int> payload;

    explicit Owner(int v) : payload(std::make_unique<int>(v)) {}
    Owner(Owner&&) noexcept = default;
    Owner& operator=(Owner&&) = delete;
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
};

} // namespace

// =============================================================================
// Criação, destruição, gerações
// =============================================================================

TEST_CASE("ecs: create devolve entidades válidas e distintas", "[ecs]")
{
    eng::ecs::World world;

    const auto a = world.create();
    const auto b = world.create();

    CHECK(world.valid(a));
    CHECK(world.valid(b));
    CHECK(a != b);
    CHECK(world.size() == 2);
}

TEST_CASE("ecs: destroy invalida o handle e limpa componentes", "[ecs]")
{
    eng::ecs::World world;

    const auto e = world.create();
    (void)world.emplace<Position>(e, 1.0f, 2.0f);
    (void)world.emplace<Velocity>(e, 0.5f, 0.0f);

    CHECK(world.componentCount<Position>() == 1);
    CHECK(world.componentCount<Velocity>() == 1);

    REQUIRE(world.destroy(e));
    CHECK_FALSE(world.valid(e));
    CHECK(world.size() == 0);
    CHECK(world.componentCount<Position>() == 0); // destruição limpa TUDO
    CHECK(world.componentCount<Velocity>() == 0);
    CHECK(world.get<Position>(e) == nullptr);
}

TEST_CASE("ecs: destruir handle obsoleto é no-op (false)", "[ecs]")
{
    eng::ecs::World world;

    const auto e = world.create();
    REQUIRE(world.destroy(e));
    CHECK_FALSE(world.destroy(e)); // segunda destruição: no-op
}

TEST_CASE("ecs: índice é reciclado com geração INCREMENTADA", "[ecs]")
{
    eng::ecs::World world;

    const auto a = world.create();
    const std::uint32_t reusedIndex = a.index;
    REQUIRE(world.destroy(a));

    const auto b = world.create();
    CHECK(b.index == reusedIndex);       // reciclou o slot
    CHECK(b.generation == a.generation + 1); // geração avançou
    CHECK(world.valid(b));
    CHECK_FALSE(world.valid(a));         // handle antigo obsoleto
    CHECK(a != b);

    // b é uma entidade NOVA: componentes de a não ressuscitam
    CHECK(world.get<Position>(a) == nullptr);
    (void)world.emplace<Position>(b, 5.0f, 5.0f);
    CHECK(world.get<Position>(a) == nullptr); // a não vê o componente de b
    CHECK(world.get<Position>(b) != nullptr);
}

TEST_CASE("ecs: handle obsoleto — todas as operações são no-op seguro", "[ecs]")
{
    eng::ecs::World world;

    const auto stale = world.create();
    REQUIRE(world.destroy(stale));

    CHECK_FALSE(world.valid(stale));
    CHECK(world.get<Position>(stale) == nullptr);
    CHECK_FALSE(world.has<Position>(stale));
    CHECK(world.emplace<Position>(stale, 1.0f, 1.0f) == nullptr);
    CHECK_FALSE(world.remove<Position>(stale));
    CHECK_FALSE(world.destroy(stale));
    CHECK(world.componentCount<Position>() == 0); // nada foi criado
}

// =============================================================================
// emplace/remove/get/has
// =============================================================================

TEST_CASE("ecs: emplace/get/has/remoção básicos", "[ecs]")
{
    eng::ecs::World world;

    const auto e = world.create();

    auto* pos = world.emplace<Position>(e, 3.0f, 4.0f);
    REQUIRE(pos != nullptr);
    CHECK(pos->x == 3.0f);
    CHECK(pos->y == 4.0f);

    CHECK(world.has<Position>(e));
    CHECK(world.get<Position>(e) == pos); // ponteiro estável
    CHECK(world.componentCount<Position>() == 1);

    CHECK(world.remove<Position>(e));
    CHECK_FALSE(world.has<Position>(e));
    CHECK(world.get<Position>(e) == nullptr);
    CHECK(world.componentCount<Position>() == 0);

    CHECK_FALSE(world.remove<Position>(e)); // remover de novo: no-op
}

TEST_CASE("ecs: emplace em entidade que já tem T SUBSTITUI", "[ecs]")
{
    eng::ecs::World world;

    const auto e = world.create();
    auto* first = world.emplace<Position>(e, 1.0f, 1.0f);
    REQUIRE(first != nullptr);

    auto* second = world.emplace<Position>(e, 9.0f, 8.0f);
    REQUIRE(second != nullptr);
    CHECK(second == first);                      // mesma vaga (dense estável)
    CHECK(world.get<Position>(e)->x == 9.0f);   // valor NOVO
    CHECK(world.componentCount<Position>() == 1); // um único componente
}

TEST_CASE("ecs: tipos distintos são independentes", "[ecs]")
{
    eng::ecs::World world;

    const auto a = world.create();
    const auto b = world.create();

    (void)world.emplace<Position>(a, 1.0f, 0.0f);
    (void)world.emplace<Velocity>(b, 2.0f, 0.0f);

    CHECK(world.has<Position>(a));
    CHECK_FALSE(world.has<Position>(b));
    CHECK(world.has<Velocity>(b));
    CHECK_FALSE(world.has<Velocity>(a));
    CHECK(world.componentCount<Position>() == 1);
    CHECK(world.componentCount<Velocity>() == 1);
}

TEST_CASE("ecs: componente move-only funciona (swap-and-pop por move-ctor)", "[ecs]")
{
    eng::ecs::World world;

    const auto a = world.create();
    const auto b = world.create();
    const auto c = world.create();

    (void)world.emplace<Owner>(a, 1);
    (void)world.emplace<Owner>(b, 2);
    (void)world.emplace<Owner>(c, 3);
    CHECK(world.componentCount<Owner>() == 3);

    REQUIRE(world.remove<Owner>(b)); // swap-and-pop move o último para a vaga
    CHECK(world.componentCount<Owner>() == 2);

    CHECK(*world.get<Owner>(a)->payload == 1);
    CHECK(*world.get<Owner>(c)->payload == 3); // movido corretamente
    CHECK(world.get<Owner>(b) == nullptr);

    // destruição de entidade move-only também remove
    REQUIRE(world.destroy(c));
    CHECK(world.componentCount<Owner>() == 1);
    CHECK(*world.get<Owner>(a)->payload == 1);
}

// =============================================================================
// each
// =============================================================================

TEST_CASE("ecs: each<T> visita todos, em ordem de inserção", "[ecs]")
{
    eng::ecs::World world;

    std::vector<eng::ecs::Entity> created;
    for (int i = 0; i < 5; ++i) {
        const auto e = world.create();
        created.push_back(e);
        (void)world.emplace<Position>(e, static_cast<float>(i), 0.0f);
    }

    std::vector<eng::ecs::Entity> visited;
    world.each<Position>([&](eng::ecs::Entity e, Position& p) {
        visited.push_back(e);
        p.x += 100.0f; // mutação permitida na versão não-const
    });

    REQUIRE(visited.size() == 5);
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(visited[i] == created[i]); // ordem de inserção
        CHECK(world.get<Position>(created[i])->x == static_cast<float>(i) + 100.0f);
    }
}

TEST_CASE("ecs: each<T,U> exige AMBOS os componentes", "[ecs]")
{
    eng::ecs::World world;

    const auto both = world.create();
    const auto onlyPos = world.create();
    const auto onlyVel = world.create();
    (void)world.emplace<Position>(both, 1.0f, 1.0f);
    (void)world.emplace<Velocity>(both, 0.1f, 0.1f);
    (void)world.emplace<Position>(onlyPos, 2.0f, 2.0f);
    (void)world.emplace<Velocity>(onlyVel, 0.2f, 0.2f);

    std::unordered_set<eng::ecs::Entity> visited;
    world.each<Position, Velocity>(
        [&](eng::ecs::Entity e, Position& p, Velocity& v) {
            visited.insert(e);
            p.x += v.dx; // integração de movimento — uso canônico
            p.y += v.dy;
        });

    CHECK(visited.size() == 1);
    CHECK(visited.count(both) == 1);
    CHECK(world.get<Position>(both)->x == 1.1f);
    CHECK(world.get<Position>(onlyPos)->x == 2.0f); // intocada
}

TEST_CASE("ecs: each const entrega const Ts& e não muta", "[ecs]")
{
    eng::ecs::World world;

    const auto e = world.create();
    (void)world.emplace<Position>(e, 7.0f, 8.0f);

    const eng::ecs::World& constWorld = world;
    float sumX = 0.0f;
    float sumY = 0.0f;
    constWorld.each<Position>([&](eng::ecs::Entity entity, const Position& p) {
        CHECK(entity == e);
        sumX += p.x;
        sumY += p.y;
    });
    CHECK(sumX == 7.0f);
    CHECK(sumY == 8.0f);
    CHECK(world.get<Position>(e)->x == 7.0f); // intocado
}

TEST_CASE("ecs: each sem matches é vazio; tipo nunca usado é vazio", "[ecs]")
{
    eng::ecs::World world;

    const auto e = world.create(); // sem componentes
    (void)e;

    int calls = 0;
    world.each<Position>([&](eng::ecs::Entity, Position&) { ++calls; });
    world.each<Position, Velocity>([&](eng::ecs::Entity, Position&, Velocity&) { ++calls; });
    CHECK(calls == 0);

    world.each<Tag>([&](eng::ecs::Entity, Tag&) { ++calls; }); // pool inexistente
    CHECK(calls == 0);
}

// =============================================================================
// Mutação durante each (snapshot)
// =============================================================================

TEST_CASE("ecs: destruir a entidade CORRENTE durante each é seguro", "[ecs]")
{
    eng::ecs::World world;

    for (int i = 0; i < 6; ++i) {
        const auto e = world.create();
        (void)world.emplace<Position>(e, 0.0f, 0.0f);
    }

    int visited = 0;
    world.each<Position>([&](eng::ecs::Entity e, Position&) {
        ++visited;
        world.destroy(e); // destrói a si mesma
    });
    CHECK(visited == 6); // todas visitadas exatamente uma vez
    CHECK(world.size() == 0);
    CHECK(world.componentCount<Position>() == 0);
}

TEST_CASE("ecs: destruir OUTRAS entidades durante each é seguro", "[ecs]")
{
    eng::ecs::World world;

    std::vector<eng::ecs::Entity> all;
    for (int i = 0; i < 8; ++i) {
        const auto e = world.create();
        all.push_back(e);
        (void)world.emplace<Position>(e, 0.0f, 0.0f);
    }

    int visited = 0;
    world.each<Position>([&](eng::ecs::Entity e, Position&) {
        ++visited;
        // o visitante destrói uma entidade futura (não visitada ainda):
        if (e == all[0] && world.valid(all[7])) {
            world.destroy(all[7]);
        }
    });
    CHECK(visited == 7); // 7 sobreviveu? NÃO: foi removida antes de ser visitada
    CHECK(world.size() == 7);
    CHECK(world.componentCount<Position>() == 7);
}

TEST_CASE("ecs: entidades criadas durante each não aparecem na rodada", "[ecs]")
{
    eng::ecs::World world;

    for (int i = 0; i < 3; ++i) {
        const auto e = world.create();
        (void)world.emplace<Position>(e, 0.0f, 0.0f);
    }

    int visited = 0;
    world.each<Position>([&](eng::ecs::Entity, Position&) {
        ++visited;
        while (world.size() < 10) { // preenche até 10 na primeira visita
            const auto nova = world.create();
            (void)world.emplace<Position>(nova, 0.0f, 0.0f);
        }
    });
    CHECK(visited == 3); // só as 3 originais nesta rodada
    CHECK(world.size() == 10); // mas foram criadas de fato

    int segunda = 0;
    world.each<Position>([&](eng::ecs::Entity, Position&) { ++segunda; });
    CHECK(segunda == 10); // na próxima rodada, todas
}

TEST_CASE("ecs: remover componente durante each de outro tipo é seguro", "[ecs]")
{
    eng::ecs::World world;

    for (int i = 0; i < 5; ++i) {
        const auto e = world.create();
        (void)world.emplace<Position>(e, 0.0f, 0.0f);
        (void)world.emplace<Velocity>(e, 1.0f, 0.0f);
    }

    int visited = 0;
    world.each<Position>([&](eng::ecs::Entity e, Position&) {
        ++visited;
        world.remove<Velocity>(e); // remove o par durante a iteração de Position
    });
    CHECK(visited == 5);
    CHECK(world.componentCount<Position>() == 5);
    CHECK(world.componentCount<Velocity>() == 0);
}

// =============================================================================
// Estresse leve + reciclagem ampla
// =============================================================================

TEST_CASE("ecs: 1000 entidades com criação/destruição/reciclagem", "[ecs]")
{
    eng::ecs::World world;

    std::vector<eng::ecs::Entity> alive;
    for (int i = 0; i < 1000; ++i) {
        const auto e = world.create();
        (void)world.emplace<Position>(e, static_cast<float>(i), 0.0f);
        if (i % 2 == 0) {
            (void)world.emplace<Velocity>(e, 1.0f, 0.0f);
        }
        alive.push_back(e);
    }
    CHECK(world.size() == 1000);
    CHECK(world.componentCount<Position>() == 1000);
    CHECK(world.componentCount<Velocity>() == 500);

    // destrói metade (índices pares)
    for (std::size_t i = 0; i < alive.size(); i += 2) {
        CHECK(world.destroy(alive[i]));
    }
    CHECK(world.size() == 500);
    CHECK(world.componentCount<Position>() == 500);

    // recicla: cria 500 novas — todas reutilizam índices, gerações novas
    std::unordered_set<std::uint32_t> seenGenerations;
    for (int i = 0; i < 500; ++i) {
        const auto e = world.create();
        CHECK(world.valid(e));
        CHECK_FALSE(world.valid(alive[2 * static_cast<std::size_t>(i)])); // antigo morto
        seenGenerations.insert(e.generation);
        (void)world.emplace<Position>(e, 0.0f, 0.0f);
    }
    CHECK(seenGenerations.count(1) == 1); // todos reciclados têm geração 1

    // each coerente após reciclagem
    int count = 0;
    world.each<Position>([&](eng::ecs::Entity, Position&) { ++count; });
    CHECK(count == 1000); // 500 sobreviventes + 500 recicladas
}
