#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "eng/events/Events.hpp"

namespace {

struct Explosion {
    float radius = 0.0f;
    int damage = 0;
};

struct Ping {
    int seq = 0;
};

struct Pong {
    int echo = 0;
};

/// 128 bytes de captura: excede o small-buffer (48) → caminho do heap.
struct BigHandler {
    std::array<double, 16> payload{}; // 128 bytes
    int* calls = nullptr;

    void operator()(const Ping& event)
    {
        (void)event;
        if (calls != nullptr) {
            ++*calls;
        }
    }
};

} // namespace

// =============================================================================
// Subscrição e dispatch básicos
// =============================================================================

TEST_CASE("events: subscribe e publish entregam payload por const ref", "[events]")
{
    eng::events::EventBus bus;

    int received = 0;
    float radius = 0.0f;
    auto sub = bus.subscribe<Explosion>([&](const Explosion& e) {
        received = e.damage;
        radius = e.radius;
    });
    REQUIRE(sub.alive());

    bus.publish(Explosion{.radius = 3.5f, .damage = 42});

    CHECK(received == 42);
    CHECK(radius == 3.5f);
    CHECK(bus.subscriberCount<Explosion>() == 1);
}

TEST_CASE("events: publish sem subscribers é no-op", "[events]")
{
    eng::events::EventBus bus;
    bus.publish(Explosion{.radius = 1.0f, .damage = 7}); // não pode falhar
    CHECK(bus.subscriberCount<Explosion>() == 0);
}

TEST_CASE("events: múltiplos handlers executam na ordem de inscrição", "[events]")
{
    eng::events::EventBus bus;

    std::vector<std::string> order;
    auto a = bus.subscribe<Ping>([&](const Ping&) { order.push_back("a"); });
    auto b = bus.subscribe<Ping>([&](const Ping&) { order.push_back("b"); });
    auto c = bus.subscribe<Ping>([&](const Ping&) { order.push_back("c"); });
    (void)a;
    (void)b;
    (void)c;

    bus.publish(Ping{.seq = 1});

    REQUIRE(order.size() == 3);
    CHECK(order[0] == "a");
    CHECK(order[1] == "b");
    CHECK(order[2] == "c");
}

TEST_CASE("events: handlers de tipos distintos são independentes", "[events]")
{
    eng::events::EventBus bus;

    int pings = 0;
    int pongs = 0;
    auto p = bus.subscribe<Ping>([&](const Ping&) { ++pings; });
    auto q = bus.subscribe<Pong>([&](const Pong&) { ++pongs; });
    (void)p;
    (void)q;

    bus.publish(Ping{.seq = 1});
    bus.publish(Pong{.echo = 2});
    bus.publish(Ping{.seq = 3});

    CHECK(pings == 2);
    CHECK(pongs == 1);
    CHECK(bus.subscriberCount<Ping>() == 1);
    CHECK(bus.subscriberCount<Pong>() == 1);
}

TEST_CASE("events: handler com estado mutável (chamadas acumulam)", "[events]")
{
    eng::events::EventBus bus;

    int counter = 0;
    auto sub = bus.subscribe<Ping>([&counter](const Ping& e) { counter += e.seq; });
    (void)sub;

    bus.publish(Ping{.seq = 1});
    bus.publish(Ping{.seq = 2});
    bus.publish(Ping{.seq = 3});

    CHECK(counter == 6);
}

// =============================================================================
// Lifetime e cancelamento
// =============================================================================

TEST_CASE("events: destruir Subscription cancela o recebimento", "[events]")
{
    eng::events::EventBus bus;

    int calls = 0;
    {
        auto sub = bus.subscribe<Ping>([&](const Ping&) { ++calls; });
        bus.publish(Ping{.seq = 1});
        REQUIRE(calls == 1);
        CHECK(bus.subscriberCount<Ping>() == 1);
    } // destrutor cancela

    CHECK(bus.subscriberCount<Ping>() == 0);
    bus.publish(Ping{.seq = 2});
    CHECK(calls == 1); // nada mais chamado
}

TEST_CASE("events: unsubscribe() explícito é idempotente", "[events]")
{
    eng::events::EventBus bus;

    int calls = 0;
    auto sub = bus.subscribe<Ping>([&](const Ping&) { ++calls; });

    sub.unsubscribe();
    CHECK_FALSE(sub.alive());
    sub.unsubscribe(); // segunda vez: no-op
    CHECK(bus.subscriberCount<Ping>() == 0);

    bus.publish(Ping{.seq = 1});
    CHECK(calls == 0);
}

TEST_CASE("events: Subscription é move-only — movimento transfere o cancelamento", "[events]")
{
    eng::events::EventBus bus;

    int calls = 0;
    std::vector<eng::events::Subscription> owned;

    {
        auto sub = bus.subscribe<Ping>([&](const Ping&) { ++calls; });
        REQUIRE(sub.alive());
        owned.push_back(std::move(sub)); // push_back move
        CHECK_FALSE(sub.alive());        // origem esvaziada
    } // destrutor da origem movida-de NÃO cancela

    CHECK(bus.subscriberCount<Ping>() == 1);
    bus.publish(Ping{.seq = 1});
    CHECK(calls == 1);

    // Movimento por atribuição: cancela o alvo anterior e assume o novo.
    auto sub2 = bus.subscribe<Ping>([&](const Ping&) { ++calls; });
    owned[0] = std::move(sub2); // cancela a 1ª inscrição, assume a 2ª
    CHECK(bus.subscriberCount<Ping>() == 1);

    owned.clear(); // cancela a 2ª (única viva)
    CHECK(bus.subscriberCount<Ping>() == 0);
    bus.publish(Ping{.seq = 2});
    CHECK(calls == 1);
}

// =============================================================================
// Mutações durante o dispatch
// =============================================================================

TEST_CASE("events: desinscrever handler não-chamado durante o dispatch", "[events]")
{
    eng::events::EventBus bus;

    std::vector<char> order;
    eng::events::Subscription victim;

    auto a = bus.subscribe<Ping>([&](const Ping&) {
        order.push_back('a');
        victim.unsubscribe(); // cancela B antes de B rodar nesta rodada
    });
    victim = bus.subscribe<Ping>([&](const Ping&) { order.push_back('b'); });
    (void)a;

    bus.publish(Ping{.seq = 1});

    CHECK(order.size() == 1);
    CHECK(order[0] == 'a');
    CHECK(bus.subscriberCount<Ping>() == 1); // B foi embora

    bus.publish(Ping{.seq = 2});
    REQUIRE(order.size() == 2);
    CHECK(order[1] == 'a'); // B continua cancelado
}

TEST_CASE("events: handler desinscreve a si próprio durante execução (seguro)", "[events]")
{
    eng::events::EventBus bus;

    int calls = 0;
    eng::events::Subscription self;
    self = bus.subscribe<Ping>([&](const Ping&) {
        ++calls;
        self.unsubscribe(); // cancela a si mesmo ENQUANTO roda
    });

    bus.publish(Ping{.seq = 1});
    CHECK(calls == 1);
    CHECK(bus.subscriberCount<Ping>() == 0);

    bus.publish(Ping{.seq = 2});
    CHECK(calls == 1); // não volta a ser chamado

    // Destroying an already-canceled Subscription: no-op (idempotência).
}

TEST_CASE("events: inscrever durante o dispatch não roda na rodada corrente", "[events]")
{
    eng::events::EventBus bus;

    int outerCalls = 0;
    int innerCalls = 0;
    eng::events::Subscription inner;

    {
        auto outer = bus.subscribe<Ping>([&](const Ping&) {
            ++outerCalls;
            if (!inner.alive()) {
                inner = bus.subscribe<Ping>([&](const Ping&) { ++innerCalls; });
            }
        });
        bus.publish(Ping{.seq = 1});
    }

    CHECK(outerCalls == 1);
    CHECK(innerCalls == 0); // NÃO chamado na rodada em que nasceu
    CHECK(bus.subscriberCount<Ping>() == 1); // sobrevive ao outer: inner viva

    bus.publish(Ping{.seq = 2});
    CHECK(outerCalls == 1); // outer foi destruído (escopo)
    CHECK(innerCalls == 1); // inner roda na rodada seguinte
}

// =============================================================================
// Reentrância (publish aninhado)
// =============================================================================

TEST_CASE("events: publish aninhado do MESMO tipo (reentrância suportada)", "[events]")
{
    eng::events::EventBus bus;

    // Log de (profundidade, seq) por invocação do handler.
    std::vector<std::pair<int, int>> log;
    int depth = 0;

    auto sub = bus.subscribe<Ping>([&](const Ping& e) {
        log.emplace_back(depth, e.seq);
        if (e.seq == 1 && depth == 0) {
            ++depth;
            bus.publish(Ping{.seq = 2}); // aninhado: NOVA rodada do mesmo slot
            --depth;
        }
    });
    (void)sub;

    bus.publish(Ping{.seq = 1});

    // Rodada externa invoca o handler com seq=1; o handler abre rodada
    // aninhada que invoca o MESMO handler com seq=2 (entradas são por
    // inscrição, não por evento); a rodada externa não re-invoca.
    REQUIRE(log.size() == 2);
    CHECK(log[0].first == 0);
    CHECK(log[0].second == 1);
    CHECK(log[1].first == 1);
    CHECK(log[1].second == 2);
}

TEST_CASE("events: publish aninhado de outro tipo despacha ambos", "[events]")
{
    eng::events::EventBus bus;

    std::vector<std::string> order;

    auto ping = bus.subscribe<Ping>([&](const Ping&) {
        order.push_back("ping");
        bus.publish(Pong{.echo = 1}); // cross-type dentro do handler
    });
    auto pong = bus.subscribe<Pong>([&](const Pong&) { order.push_back("pong"); });
    (void)ping;
    (void)pong;

    bus.publish(Ping{.seq = 1});

    REQUIRE(order.size() == 2);
    CHECK(order[0] == "ping");
    CHECK(order[1] == "pong");
}

TEST_CASE("events: dispatch aninhado sem dangling — cancelamentos cruzados", "[events]")
{
    eng::events::EventBus bus;

    std::vector<char> order;
    eng::events::Subscription subB;
    eng::events::Subscription subC;

    auto a = bus.subscribe<Ping>([&](const Ping& e) {
        order.push_back('a');
        if (e.seq == 1) {                 // só na primeira rodada
            subC.unsubscribe();           // cancela C (não chamada nesta rodada)
            bus.publish(Pong{.echo = 9}); // rodada aninhada de outro tipo
        }
    });
    subB = bus.subscribe<Ping>([&](const Ping&) { order.push_back('b'); });
    subC = bus.subscribe<Ping>([&](const Ping&) { order.push_back('c'); });

    auto pongHandler = bus.subscribe<Pong>([&](const Pong&) {
        order.push_back('P');
        subB.unsubscribe(); // cancela B de DENTRO do dispatch de Pong
    });
    (void)pongHandler;
    (void)a;

    bus.publish(Ping{.seq = 1});

    // Rodada de Ping: a roda (cancela C, dispara Pong: P roda e cancela B),
    // B e C mortos — não rodam. Nenhum acesso a memória liberada.
    REQUIRE(order.size() == 2);
    CHECK(order[0] == 'a');
    CHECK(order[1] == 'P');
    CHECK(bus.subscriberCount<Ping>() == 1); // só A sobrevive
    CHECK(bus.subscriberCount<Pong>() == 1);

    bus.publish(Ping{.seq = 2});
    REQUIRE(order.size() == 3);
    CHECK(order[2] == 'a');
}

// =============================================================================
// Handler grande (caminho heap do small-buffer) e contagem
// =============================================================================

TEST_CASE("events: handler maior que o small-buffer usa heap e funciona", "[events]")
{
    eng::events::EventBus bus;

    int calls = 0;
    BigHandler big;
    big.payload.fill(1.5);
    big.calls = &calls;

    {
        auto sub = bus.subscribe<Ping>(big); // cópia de 128+8 bytes → heap
        bus.publish(Ping{.seq = 5});
        CHECK(calls == 1);
    }

    CHECK(bus.subscriberCount<Ping>() == 0);
    bus.publish(Ping{.seq = 6});
    CHECK(calls == 1);
}

TEST_CASE("events: subscriberCount acompanha subscrições e cancelamentos", "[events]")
{
    eng::events::EventBus bus;

    CHECK(bus.subscriberCount<Ping>() == 0);

    std::vector<eng::events::Subscription> subs;
    for (int i = 0; i < 5; ++i) {
        subs.push_back(bus.subscribe<Ping>([](const Ping&) {}));
        CHECK(bus.subscriberCount<Ping>() == static_cast<std::size_t>(i) + 1);
    }

    subs.pop_back(); // ~Subscription cancela
    CHECK(bus.subscriberCount<Ping>() == 4);

    subs.clear();
    CHECK(bus.subscriberCount<Ping>() == 0);
}
