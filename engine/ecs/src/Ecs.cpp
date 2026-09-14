#include "eng/ecs/Ecs.hpp"

namespace eng::ecs {

Entity World::create()
{
    if (!freeList_.empty()) {
        const std::uint32_t index = freeList_.back();
        freeList_.pop_back();
        Slot& slot = slots_[index];
        slot.alive = true; // geração já incrementada no destroy do ciclo anterior
        ++aliveCount_;
        return Entity{index, slot.generation};
    }
    const auto index = static_cast<std::uint32_t>(slots_.size());
    slots_.push_back(Slot{0u, true});
    ++aliveCount_;
    return Entity{index, 0u};
}

bool World::destroy(Entity e)
{
    if (!valid(e)) {
        return false; // handle obsoleto: no-op seguro
    }
    for (auto& entry : pools_) {
        entry.second->remove(e); // virtual: swap-and-pop tipado
    }
    Slot& slot = slots_[e.index];
    slot.alive = false;
    slot.generation += 1u; // invalida handles antigos (wrap em 2^32 documentado, ADR-024)
    freeList_.push_back(e.index);
    --aliveCount_;
    return true;
}

bool World::valid(Entity e) const noexcept
{
    return e.index < slots_.size()
           && slots_[e.index].alive
           && slots_[e.index].generation == e.generation;
}

} // namespace eng::ecs
