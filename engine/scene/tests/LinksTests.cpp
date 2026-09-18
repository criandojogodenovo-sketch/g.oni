#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "eng/scene/Links.hpp"
#include "eng/scene/Scene.hpp"
#include "eng/scene/SceneSerializer.hpp"

// =============================================================================
// Tipos
// =============================================================================

TEST_CASE("links: registro de tipos — vazio/duplicado rejeitados", "[scene][links]")
{
    eng::scene::LinkRegistry registry;

    CHECK(registry.registerType("", {}).isError());         // nome vazio
    CHECK(registry.registerType("", {true}).isError());  // vazio (flags não salvam)

    CHECK(registry.registerType("target", {}).ok());
    CHECK(registry.hasType("target"));
    CHECK(registry.typeFlags("target") != nullptr);
    CHECK(registry.typeFlags("target")->hierarchical == false);

    // duplicado → erro
    const auto duplicate = registry.registerType("target", {true});
    REQUIRE(duplicate.isError());
    CHECK(duplicate.error().message.find("já registrado") !=
          std::string::npos);
    // flags NÃO mudaram (primeiro vence)
    CHECK(registry.typeFlags("target")->hierarchical == false);
}

TEST_CASE("links: tipos em ordem alfabética (serialização determinística)", "[scene][links]")
{
    eng::scene::LinkRegistry registry;
    REQUIRE(registry.registerType("zeta", {}).ok());
    REQUIRE(registry.registerType("alpha", {true}).ok());
    REQUIRE(registry.registerType("mid", {}).ok());

    const auto types = registry.types();
    REQUIRE(types.size() == 3);
    CHECK(types[0].first == "alpha");
    CHECK(types[1].first == "mid");
    CHECK(types[2].first == "zeta");
}

// =============================================================================
// Ciclo de vida
// =============================================================================

TEST_CASE("links: create valida tipo/self/duplicado", "[scene][links]")
{
    eng::scene::Scene scene;
    REQUIRE(scene.links().registerType("target", {}).ok());

    const auto a = scene.createNode();
    const auto b = scene.createNode();

    // tipo não registrado
    CHECK(scene.createLink("mira", a, b).isError());
    // self-link
    CHECK(scene.createLink("target", a, a).isError());
    // ok
    const auto link = scene.createLink("target", a, b);
    REQUIRE(link.ok());
    CHECK(scene.links().size() == 1);
    // duplicado exato
    CHECK(scene.createLink("target", a, b).isError());
    // MESMO par em tipo diferente é permitido
    REQUIRE(scene.links().registerType("value", {}).ok());
    CHECK(scene.createLink("value", a, b).ok());
    // par invertido do mesmo tipo é outro link
    const auto reversed = scene.createLink("target", b, a);
    CHECK(reversed.ok());
}

TEST_CASE("links: pontas inválidas rejeitadas pela Scene", "[scene][links]")
{
    eng::scene::Scene scene;
    REQUIRE(scene.links().registerType("target", {}).ok());

    const auto a = scene.createNode();
    const eng::ecs::Entity dead{9999u, 1u};

    const auto result = scene.createLink("target", a, dead);
    REQUIRE(result.isError());
    CHECK(result.error().message.find("não é um nó vivo") != std::string::npos);
}

TEST_CASE("links: handles estáveis — destruir e criar não ressuscita", "[scene][links]")
{
    eng::scene::LinkRegistry registry;
    REQUIRE(registry.registerType("target", {}).ok());

    eng::ecs::Entity a{1u, 0u};
    eng::ecs::Entity b{2u, 0u};

    auto first = registry.create("target", a, b);
    REQUIRE(first.ok());
    const auto id = first.value();

    CHECK(registry.get(id) != nullptr);
    CHECK(registry.destroy(id));
    CHECK(registry.get(id) == nullptr);        // obsoleto
    CHECK_FALSE(registry.destroy(id));          // no-op seguro

    // Recria: handle DIFERENTE (geração avançou), mesmo conteúdo.
    auto second = registry.create("target", a, b);
    REQUIRE(second.ok());
    CHECK_FALSE(second.value() == id);
    CHECK(second.value().generation == id.generation + 1);
    CHECK(registry.get(second.value()) != nullptr);
    CHECK(registry.get(id) == nullptr);  // antigo segue morto
}

// =============================================================================
// Detecção de ciclo (links hierárquicos)
// =============================================================================

TEST_CASE("links: ciclo hierárquico rejeitado (cadeia e triângulo)", "[scene][links]")
{
    eng::scene::Scene scene;
    REQUIRE(scene.links().registerType("child_of", {true}).ok());

    const auto a = scene.createNode();
    const auto b = scene.createNode();
    const auto c = scene.createNode();

    // cadeia a→b→c: fechar c→a é ciclo
    REQUIRE(scene.createLink("child_of", a, b).ok());
    REQUIRE(scene.createLink("child_of", b, c).ok());
    const auto cycle = scene.createLink("child_of", c, a);
    REQUIRE(cycle.isError());
    CHECK(cycle.error().message.find("ciclo") != std::string::npos);

    // triângulo não-fechado ainda ok: b→a NÃO é ciclo (a→b existe, mas
    // b→a... espere: a→b e b→a É ciclo de 2). Teste direto:
    const auto cycle2 = scene.createLink("child_of", b, a);
    CHECK(cycle2.isError());

    // c→b é permitido? b→c existe, c→b fecharia ciclo de 2 — rejeitado.
    CHECK(scene.createLink("child_of", c, b).isError());
}

TEST_CASE("links: ciclo que só fecha por aresta DIRECIONAL é permitido", "[scene][links]")
{
    eng::scene::Scene scene;
    REQUIRE(scene.links().registerType("hier", {true}).ok());
    REQUIRE(scene.links().registerType("ref", {false}).ok());

    const auto a = scene.createNode();
    const auto b = scene.createNode();

    // a --hier--> b; b --ref--> a: não é ciclo hierárquico.
    REQUIRE(scene.createLink("hier", a, b).ok());
    CHECK(scene.createLink("ref", b, a).ok());

    // hier a→b de novo não: hier b→a sim fecharia.
    CHECK(scene.createLink("hier", b, a).isError());
}

// =============================================================================
// Consultas e propagação BFS
// =============================================================================

TEST_CASE("links: eachFrom/eachTo/eachReachable", "[scene][links]")
{
    eng::scene::Scene scene;
    REQUIRE(scene.links().registerType("follow", {}).ok());

    const auto a = scene.createNode();
    const auto b = scene.createNode();
    const auto c = scene.createNode();
    const auto d = scene.createNode();

    // a→b→c (cadeia) e a→d (ramo)
    REQUIRE(scene.createLink("follow", a, b).ok());
    REQUIRE(scene.createLink("follow", b, c).ok());
    REQUIRE(scene.createLink("follow", a, d).ok());

    std::vector<eng::ecs::Entity> fromA;
    scene.links().eachFrom(a, [&](const eng::scene::LinkRecord& record) {
        fromA.push_back(record.to);
    });
    REQUIRE(fromA.size() == 2);
    CHECK(fromA[0] == b);
    CHECK(fromA[1] == d);

    std::vector<eng::ecs::Entity> toC;
    scene.links().eachTo(c, [&](const eng::scene::LinkRecord& record) {
        toC.push_back(record.from);
    });
    REQUIRE(toC.size() == 1);
    CHECK(toC[0] == b);

    // BFS transitivo: a alcança b, d (1º nível) e c (2º nível) — sem a.
    std::vector<eng::ecs::Entity> reachable;
    scene.links().eachReachable("follow", a,
                                [&](eng::ecs::Entity reached) {
                                    reachable.push_back(reached);
                                });
    CHECK(reachable.size() == 3);
    CHECK(eng::scene::LinkRegistry{}.reachable("follow", a, c) ==
          false);  // (registry vazia: só para usar o método const)

    eng::scene::Scene& s = scene;  // alcance não-const
    CHECK(s.links().reachable("follow", a, c));
    CHECK(s.links().reachable("follow", a, d));
    CHECK_FALSE(s.links().reachable("follow", c, a));
    CHECK_FALSE(s.links().reachable("follow", a, a));
}

// =============================================================================
// Destruição
// =============================================================================

TEST_CASE("links: destroyNode remove os links da subárvore (as pontas)", "[scene][links]")
{
    eng::scene::Scene scene;
    REQUIRE(scene.links().registerType("target", {}).ok());

    const auto parent = scene.createNode();
    const auto child = scene.createNode();
    REQUIRE(scene.attach(child, parent));
    const auto other = scene.createNode();

    // Links que TOCAM a subárvore morrem: child→other, other→parent.
    REQUIRE(scene.createLink("target", child, other).ok());
    REQUIRE(scene.createLink("target", other, parent).ok());
    REQUIRE(scene.createLink("target", other, other).isError());  // self
    CHECK(scene.links().size() == 2);

    REQUIRE(scene.destroyNode(parent));  // leva child junto
    CHECK(scene.links().size() == 0);    // ambos links tinham ponta morta
}

TEST_CASE("links: sweep varre pontas mortas do bypass do world", "[scene][links]")
{
    eng::scene::Scene scene;
    REQUIRE(scene.links().registerType("target", {}).ok());

    const auto a = scene.createNode();
    const auto b = scene.createNode();
    const auto c = scene.createNode();

    REQUIRE(scene.createLink("target", a, b).ok());
    REQUIRE(scene.createLink("target", a, c).ok());

    // BYPASS: destrói direto no world (caminho NÃO suportado).
    scene.world().destroy(b);

    // Consultas veem o link morto (registry é grafo puro)...
    CHECK(scene.links().size() == 2);
    // ...até o sweep varrer.
    CHECK(scene.sweepLinks() == 1);
    CHECK(scene.links().size() == 1);

    // O link sobrevivente continua utilizável.
    std::vector<eng::ecs::Entity> fromA;
    scene.links().eachFrom(a, [&](const eng::scene::LinkRecord& record) {
        fromA.push_back(record.to);
    });
    REQUIRE(fromA.size() == 1);
    CHECK(fromA[0] == c);
}

// =============================================================================
// Serialização (round-trip pelas seções do SceneSerializer — ADR-051)
// =============================================================================

TEST_CASE("links: round-trip save/load preserva tipos, ordem e pontas", "[scene][links]")
{
    eng::scene::Scene scene;
    REQUIRE(scene.links().registerType("target", {false}).ok());
    REQUIRE(scene.links().registerType("child_of", {true}).ok());

    const auto a = scene.createNode();
    const auto b = scene.createNode();
    const auto c = scene.createNode();

    auto ab = scene.createLink("target", a, b);
    REQUIRE(ab.ok());
    auto bc = scene.createLink("child_of", b, c);
    REQUIRE(bc.ok());

    const auto saved = eng::scene::SceneSerializer::save(scene);
    REQUIRE(saved.ok());

    eng::scene::Scene loaded;
    const auto result = eng::scene::SceneSerializer::load(loaded, saved.value());
    REQUIRE(result.ok());

    CHECK(loaded.links().size() == 2);
    CHECK(loaded.links().hasType("target"));
    CHECK(loaded.links().hasType("child_of"));
    CHECK(loaded.links().typeFlags("child_of")->hierarchical);

    // Pontas preservadas — sem ASSUMIR ordem: o save ordena entidades por
    // SceneEntityId canônico (ordem de uuid, NÃO de criação), então a
    // identidade por posição no arquivo é inválida. A topologia é a
    // prova: target X→Y; child_of Y→Z; Y é ponta compartilhada (to de um,
    // from do outro) — a cadeia a→b→c sobreviveu íntegra.
    std::optional<eng::scene::LinkRecord> target;
    std::optional<eng::scene::LinkRecord> childOf;
    loaded.links().each(
        [&](eng::scene::LinkId, const eng::scene::LinkRecord& r) {
            if (r.type == "target") {
                target = r;
            } else if (r.type == "child_of") {
                childOf = r;
            }
        });
    REQUIRE(target.has_value());
    REQUIRE(childOf.has_value());
    CHECK(target->from != target->to);      // self-link teria sido rejeitado
    CHECK(childOf->from != childOf->to);
    CHECK(target->to == childOf->from);      // b: elo da cadeia
    CHECK(target->from != childOf->to);      // a ≠ c
    // Pontas são nós VIVOS da cena carregada.
    CHECK(loaded.world().valid(target->from));
    CHECK(loaded.world().valid(target->to));
    CHECK(loaded.world().valid(childOf->to));

    // SAVE DETERMINÍSTA: save(load(save)) idêntico byte-a-byte.
    const auto resaved = eng::scene::SceneSerializer::save(loaded);
    REQUIRE(resaved.ok());
    CHECK(resaved.value() == saved.value());
}

TEST_CASE("links: load com ponta ausente é ParseError", "[scene][links]")
{
    eng::scene::Scene scene;
    REQUIRE(scene.links().registerType("target", {}).ok());
    const auto a = scene.createNode();
    const auto b = scene.createNode();
    REQUIRE(scene.createLink("target", a, b).ok());

    const auto saved = eng::scene::SceneSerializer::save(scene);
    REQUIRE(saved.ok());

    // Remove a entidade B do texto (id dela some de entities/ids) —
    // simula arquivo editado à mão com from órfão.
    std::string text = saved.value();
    // forma canônica: as duas entidades estão em "entities". Simples:
    // carrega num arquivo e remove o segundo nó pela metade não é fácil
    // de fazer por string — carrega numa cena, remove B, salva de novo
    // SEM o link (o sweep remove), e então injeta o link órfão... O
    // caminho direto: parse manual do JSON é trabalho do serializer.
    // Aqui o teste do ÓRFÃO é feito remontando o JSON com from inexistente.
    (void)text;

    eng::scene::Scene scene2;
    const auto b2 = scene2.createNode();  // uma entidade só
    (void)b2;
    auto saved2 = eng::scene::SceneSerializer::save(scene2);
    REQUIRE(saved2.ok());
    // Injeta um from que não existe: usa um uuid inventado.
    const std::string withOrphan = saved2.value().substr(0, saved2.value().size() - 1) +
        ",\"links\":{\"linkTypes\":[{\"type\":\"target\",\"hierarchical\":false}],"
        "\"entries\":[{\"type\":\"target\",\"from\":\"00000000-0000-4000-8000-000000000000\","
        "\"to\":\"" + "00000000-0000-4000-8000-000000000000" + "\"}]}}";
    eng::scene::Scene loaded;
    const auto result =
        eng::scene::SceneSerializer::load(loaded, withOrphan);
    REQUIRE(result.isError());
    CHECK(result.error().message.find("não existe na cena") !=
          std::string::npos);
}
