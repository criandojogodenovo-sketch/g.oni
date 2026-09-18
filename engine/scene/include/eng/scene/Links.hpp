#pragma once

/// eng::scene::LinkRegistry — links tipados entre entidades com handles
/// estáveis e detecção de ciclos (evolução P0-5; ADR-051).
///
/// Modelo (decisões completas em ADR-051):
///   - Um link é um registro (tipo, from, to) na registry da Scene —
///     relação ENTRE entidades que não é parent/child (ex.: "target",
///     "follow", "value"). A registry é grafo PURO: não valida Entity
///     (quem valida é a Scene, que conhece o World).
///   - **Tipado**: tipos registrados antecipadamente (flags: hierárquico
///     = participa da detecção de ciclos). create() com tipo não
///     registrado é ERRO — não existe "link genérico".
///   - **Handles estáveis**: LinkId{index, generation} com free-list;
///     destruir + criar NÃO ressuscita handles antigos (mesma técnica do
///     ECS, ADR-024). Operações com handle obsoleto são no-op seguro.
///   - **Ciclos**: criar link hierárquico from→to falha se `to` já
///     alcança `from` por links hierárquicos (DFS/BFS). Links direcionais
///     não participam do grafo de ciclos (ciclo que só fecha por aresta
///     direcional é PERMITIDO por design).
///   - Self-link proibido; (tipo, from, to) duplicado proibido.
///   - **Propagação**: eachReachable(type, from, fn) — BFS transitivo
///     sobre links do tipo (paridade com a engine TS legada).
///   - **Destruição**: destroyAllFor(entity) chamado pela Scene ao
///     destruir nós; pontas mortas por bypass (world().destroy()) são
///     varridas por sweep(const World&) — tolerância oportunista
///     (mesma política da Hierarchy, ADR-025).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "eng/core/Result.hpp"
#include "eng/ecs/Ecs.hpp"

namespace eng::scene {

/// Handle de link: índice do slot + geração (detecção de obsolescência).
struct LinkId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] friend bool operator==(const LinkId&,
                                         const LinkId&) = default;
    [[nodiscard]] friend bool operator!=(const LinkId&,
                                         const LinkId&) = default;
};

/// Flags de um TIPO de link (registro antecipado).
struct LinkTypeFlags {
    /// Participa da detecção de ciclos (relações "dono de", "filho de"
    /// conceitual). false = direcional puro (referência, mira).
    bool hierarchical = false;
};

/// Registro vivo de um link (leitura — mutação via create/destroy).
struct LinkRecord {
    std::string type;
    eng::ecs::Entity from{};
    eng::ecs::Entity to{};
};

class LinkRegistry final {
public:
    LinkRegistry() = default;
    ~LinkRegistry() = default;
    LinkRegistry(const LinkRegistry&) = delete;
    LinkRegistry& operator=(const LinkRegistry&) = delete;
    LinkRegistry(LinkRegistry&&) noexcept = default;
    LinkRegistry& operator=(LinkRegistry&&) noexcept = default;

    // --- tipos ---------------------------------------------------------------

    /// Registra um tipo de link. Erros: nome vazio, duplicado.
    [[nodiscard]] eng::core::Result<void> registerType(
        std::string_view type, LinkTypeFlags flags);

    [[nodiscard]] bool hasType(std::string_view type) const;
    [[nodiscard]] const LinkTypeFlags* typeFlags(std::string_view type) const;

    /// Tipos registrados em ordem alfabética (determinístico —
    /// serialização/diagnóstico).
    [[nodiscard]] std::vector<std::pair<std::string, LinkTypeFlags>> types()
        const;

    // --- ciclo de vida dos links ----------------------------------------------

    /// Cria o link (tipo, from, to). Erros: tipo não registrado,
    /// from == to, duplicado exato, ou ciclo (tipo hierárquico).
    [[nodiscard]] eng::core::Result<LinkId> create(std::string_view type,
                                                   eng::ecs::Entity from,
                                                   eng::ecs::Entity to);

    /// Destrói o link. false: handle obsoleto (no-op seguro).
    bool destroy(LinkId id);

    /// Destrói TODOS os links que tocam `entity` (uma das pontas).
    /// Retorna quantos foram destruídos.
    std::size_t destroyAllFor(eng::ecs::Entity entity);

    /// Remove links com ponta morta (bypass do world). Retorna quantos.
    std::size_t sweep(const eng::ecs::World& world);

    /// Registro do link (nullptr se handle obsoleto).
    [[nodiscard]] const LinkRecord* get(LinkId id) const;

    // --- consultas -------------------------------------------------------------

    /// Links cuja origem é `from` (todos os tipos), em ordem de criação.
    template<typename Fn>
    void eachFrom(eng::ecs::Entity from, Fn&& fn) const
    {
        static_assert(
            std::is_invocable_v<Fn&, const LinkRecord&>,
            "fn deve ser invocável como fn(const LinkRecord&)");
        for (const Slot& slot : slots_) {
            if (slot.alive && slot.record.from == from) {
                fn(slot.record);
            }
        }
    }

    /// Links cujo destino é `to` (todos os tipos), em ordem de criação.
    template<typename Fn>
    void eachTo(eng::ecs::Entity to, Fn&& fn) const
    {
        static_assert(
            std::is_invocable_v<Fn&, const LinkRecord&>,
            "fn deve ser invocável como fn(const LinkRecord&)");
        for (const Slot& slot : slots_) {
            if (slot.alive && slot.record.to == to) {
                fn(slot.record);
            }
        }
    }

    /// Entidades alcançáveis a partir de `from` por links do TIPO
    /// (BFS transitivo, sem repetir destino). NÃO inclui `from`.
    template<typename Fn>
    void eachReachable(std::string_view type, eng::ecs::Entity from,
                       Fn&& fn) const
    {
        static_assert(
            std::is_invocable_v<Fn&, eng::ecs::Entity>,
            "fn deve ser invocável como fn(eng::ecs::Entity)");
        if (type.empty()) {
            return;
        }
        // BFS com fila + vetor de visitados (entidades por índice).
        std::vector<eng::ecs::Entity> frontier;
        frontier.push_back(from);
        std::vector<eng::ecs::Entity> visited;
        while (!frontier.empty()) {
            const eng::ecs::Entity current = frontier.back();
            frontier.pop_back();
            for (const Slot& slot : slots_) {
                if (!slot.alive || slot.record.from != current ||
                    slot.record.type != type) {
                    continue;
                }
                const eng::ecs::Entity next = slot.record.to;
                const bool seen =
                    next == from ||
                    std::find_if(visited.begin(), visited.end(),
                                 [next](eng::ecs::Entity e) {
                                     return e == next;
                                 }) != visited.end();
                if (!seen) {
                    visited.push_back(next);
                    frontier.push_back(next);
                }
            }
        }
        for (const eng::ecs::Entity reached : visited) {
            fn(reached);
        }
    }

    /// `to` é alcançável a partir de `from` por links do tipo?
    [[nodiscard]] bool reachable(std::string_view type,
                                 eng::ecs::Entity from,
                                 eng::ecs::Entity to) const;

    /// Total de links vivos.
    [[nodiscard]] std::size_t size() const noexcept { return aliveCount_; }

    /// Todos os links vivos com handle, em ordem de criação (serialização).
    template<typename Fn>
    void each(Fn&& fn) const
    {
        static_assert(std::is_invocable_v<Fn&, LinkId, const LinkRecord&>,
                      "fn deve ser invocável como fn(LinkId, const LinkRecord&)");
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            const Slot& slot = slots_[i];
            if (slot.alive) {
                fn(LinkId{static_cast<std::uint32_t>(i), slot.generation},
                   slot.record);
            }
        }
    }

    void clear() noexcept;

private:
    struct Slot {
        LinkRecord record{};
        std::uint32_t generation = 0;
        bool alive = false;
    };

    [[nodiscard]] std::size_t freeSlot();

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> freeList_;
    std::vector<std::pair<std::string, LinkTypeFlags>> types_;
    std::size_t aliveCount_ = 0;
};

}  // namespace eng::scene
