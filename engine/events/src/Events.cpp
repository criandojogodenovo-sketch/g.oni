#include "eng/events/Events.hpp"

#include <algorithm>

namespace eng::events::detail {

// --- Handler -------------------------------------------------------------------

Handler::~Handler()
{
    if (obj_ != nullptr) {
        destroy_(obj_);
        if (heap_) {
            ::operator delete(obj_);
        }
        obj_ = nullptr;
    }
}

void Handler::invoke(const void* event)
{
    if (invoke_ != nullptr) {
        invoke_(obj_, event);
    }
}

// --- SlotBase -------------------------------------------------------------------

void SlotBase::release(Entry* entry) noexcept
{
    if (entry == nullptr) {
        return;
    }
    if (depth_ > 0) {
        // Rodada ativa: tombstone; a limpeza física ocorre no fim da rodada.
        entry->dead = true;
        return;
    }
    // Sem dispatch ativo: remoção física imediata (busca linear — O(n),
    // ADR-022: unsubscribe é operação fria em relação ao publish).
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (&*it == entry) {
            entries_.erase(it);
            return;
        }
    }
}

std::size_t SlotBase::liveCount() const noexcept
{
    std::size_t count = 0;
    for (const Entry& entry : entries_) {
        if (!entry.dead) {
            ++count;
        }
    }
    return count;
}

void SlotBase::sweepDead()
{
    entries_.remove_if([](const Entry& entry) { return entry.dead; });
}

} // namespace eng::events::detail
