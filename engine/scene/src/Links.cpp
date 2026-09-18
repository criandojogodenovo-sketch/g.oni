#include "eng/scene/Links.hpp"

/// LinkRegistry — implementação (evolução P0-5; ADR-051).
///
/// Storage: vector<Slot> + free-list de índices; geração por slot
/// incrementa a cada destruição (handles estáveis — ADR-024 aplicado a
/// links). Consultas eachFrom/eachTo são O(n) sobre os slots vivos —
/// a escala do editor (dezenas/centenas de links) não justifica índice
/// por entidade ainda (extensão registrada em docs/architecture/21).

#include <algorithm>

namespace eng::scene {

// =============================================================================
// Tipos
// =============================================================================

eng::core::Result<void> LinkRegistry::registerType(std::string_view type,
                                                   LinkTypeFlags flags)
{
    using eng::core::Error;
    using eng::core::StatusCode;

    if (type.empty()) {
        return eng::core::makeUnexpected(Error{
            StatusCode::InvalidArgument,
            "LinkRegistry::registerType: nome de tipo vazio"});
    }
    for (const auto& existing : types_) {
        if (existing.first == type) {
            return eng::core::makeUnexpected(Error{
                StatusCode::AlreadyExists,
                "LinkRegistry::registerType: tipo '" + std::string(type) +
                    "' já registrado"});
        }
    }
    types_.emplace_back(std::string(type), flags);
    // Ordem canônica: alfabética (serialização determinística).
    std::sort(types_.begin(), types_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return {};
}

bool LinkRegistry::hasType(std::string_view type) const
{
    return typeFlags(type) != nullptr;
}

const LinkTypeFlags* LinkRegistry::typeFlags(std::string_view type) const
{
    for (const auto& existing : types_) {
        if (existing.first == type) {
            return &existing.second;
        }
    }
    return nullptr;
}

std::vector<std::pair<std::string, LinkTypeFlags>> LinkRegistry::types()
    const
{
    return types_;  // já canônico (alfabético)
}

// =============================================================================
// Ciclo de vida
// =============================================================================

std::size_t LinkRegistry::freeSlot()
{
    if (freeList_.empty()) {
        slots_.emplace_back();
        return slots_.size() - 1;
    }
    const std::uint32_t index = freeList_.back();
    freeList_.pop_back();
    return index;
}

eng::core::Result<LinkId> LinkRegistry::create(std::string_view type,
                                                eng::ecs::Entity from,
                                                eng::ecs::Entity to)
{
    using eng::core::Error;
    using eng::core::StatusCode;

    const LinkTypeFlags* flags = typeFlags(type);
    if (flags == nullptr) {
        return eng::core::makeUnexpected(Error{
            StatusCode::NotFound,
            "LinkRegistry::create: tipo '" + std::string(type) +
                "' não registrado"});
    }
    if (from == to) {
        return eng::core::makeUnexpected(Error{
            StatusCode::InvalidArgument,
            "LinkRegistry::create: self-link proibido"});
    }
    for (const Slot& slot : slots_) {
        if (slot.alive && slot.record.type == type &&
            slot.record.from == from && slot.record.to == to) {
            return eng::core::makeUnexpected(Error{
                StatusCode::AlreadyExists,
                "LinkRegistry::create: link ('" + std::string(type) +
                    "') já existe entre as entidades"});
        }
    }
    if (flags->hierarchical && reachable(type, to, from)) {
        return eng::core::makeUnexpected(Error{
            StatusCode::InvalidArgument,
            "LinkRegistry::create: ciclo detectado (o destino já alcança "
            "a origem por links '" +
                std::string(type) + "')"});
    }

    const std::size_t index = freeSlot();
    Slot& slot = slots_[index];
    slot.record = LinkRecord{std::string(type), from, to};
    slot.alive = true;
    ++aliveCount_;
    return LinkId{static_cast<std::uint32_t>(index), slot.generation};
}

bool LinkRegistry::destroy(LinkId id)
{
    if (id.index >= slots_.size()) {
        return false;  // handle de outro universo: no-op seguro
    }
    Slot& slot = slots_[id.index];
    if (!slot.alive || slot.generation != id.generation) {
        return false;  // obsoleto
    }
    slot.alive = false;
    slot.record = LinkRecord{};
    ++slot.generation;
    freeList_.push_back(id.index);
    --aliveCount_;
    return true;
}

std::size_t LinkRegistry::destroyAllFor(eng::ecs::Entity entity)
{
    std::size_t removed = 0;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        Slot& slot = slots_[i];
        if (slot.alive &&
            (slot.record.from == entity || slot.record.to == entity)) {
            slot.alive = false;
            slot.record = LinkRecord{};
            ++slot.generation;
            freeList_.push_back(static_cast<std::uint32_t>(i));
            --aliveCount_;
            ++removed;
        }
    }
    return removed;
}

std::size_t LinkRegistry::sweep(const eng::ecs::World& world)
{
    std::size_t removed = 0;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        Slot& slot = slots_[i];
        if (slot.alive && (!world.valid(slot.record.from) ||
                           !world.valid(slot.record.to))) {
            slot.alive = false;
            slot.record = LinkRecord{};
            ++slot.generation;
            freeList_.push_back(static_cast<std::uint32_t>(i));
            --aliveCount_;
            ++removed;
        }
    }
    return removed;
}

const LinkRecord* LinkRegistry::get(LinkId id) const
{
    if (id.index >= slots_.size()) {
        return nullptr;
    }
    const Slot& slot = slots_[id.index];
    return slot.alive && slot.generation == id.generation ? &slot.record
                                                          : nullptr;
}

// =============================================================================
// Consultas
// =============================================================================

bool LinkRegistry::reachable(std::string_view type, eng::ecs::Entity from,
                             eng::ecs::Entity to) const
{
    if (type.empty() || from == to) {
        return false;
    }
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
            if (next == to) {
                return true;
            }
            const bool seen = std::find(visited.begin(), visited.end(),
                                        next) != visited.end();
            if (!seen) {
                visited.push_back(next);
                frontier.push_back(next);
            }
        }
    }
    return false;
}

void LinkRegistry::clear() noexcept
{
    slots_.clear();
    freeList_.clear();
    aliveCount_ = 0;
    // tipos PERMANECEM: registro de tipos é configuração, não conteúdo.
}

}  // namespace eng::scene
