#pragma once

#include <cstddef>

#include "eng/mem/Allocator.hpp"

namespace eng::mem {

/// Alocador bump/arena sobre um bloco único obtido de um alocador de backing.
///
/// Políticas documentadas:
/// - Cada alocação carrega um cabeçalho de 8 bytes (tamanho) imediatamente
///   antes do ponteiro do usuário — overhead por alocação.
/// - `deallocate` libera APENAS a última alocação (rewind de um nível);
///   liberar blocos anteriores é no-op — a memória volta no `reset()`.
/// - `reallocate` estende in-place quando o ponteiro é a última alocação e
///   cabe; caso contrário aloca um bloco novo, copia min(antigo, novo) bytes
///   e o antigo fica órfão até o reset (comportamento clássico de arena).
/// - `owns` cobre todo o range [base, base + capacidade), mesmo ponteiros
///   já liberados (é um teste de endereço, não de vida).
class ArenaAllocator final : public Allocator {
public:
    ArenaAllocator(std::size_t capacityBytes, Allocator& backing,
                   const char* name = "ArenaAllocator") noexcept;

    ~ArenaAllocator() override;

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment) noexcept override;
    void deallocate(void* ptr) noexcept override;
    [[nodiscard]] void* reallocate(void* ptr, std::size_t newSize,
                                   std::size_t alignment) noexcept override;
    [[nodiscard]] bool owns(const void* ptr) const noexcept override;
    [[nodiscard]] const char* name() const noexcept override;

    /// Zera o offset — todas as alocações anteriores morrem de uma vez.
    void reset() noexcept;

    // --- estatísticas --------------------------------------------------------

    struct Stats {
        std::size_t capacity;      ///< bytes totais do bloco
        std::size_t usedBytes;     ///< bytes consumidos (inclui padding)
        std::size_t allocationCount; ///< alocações desde o último reset
    };

    [[nodiscard]] Stats stats() const noexcept;

private:
    const char* name_;
    Allocator& backing_;
    void* base_{nullptr};
    std::size_t capacity_{0};
    std::size_t offset_{0};
    void* lastPtr_{nullptr};
    std::size_t lastStart_{0};
    std::size_t lastSize_{0};
    std::size_t allocationCount_{0};
};

} // namespace eng::mem
