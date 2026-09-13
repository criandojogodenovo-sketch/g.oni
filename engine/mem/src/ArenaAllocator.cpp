#include "eng/mem/ArenaAllocator.hpp"

#include <cstring>

namespace eng::mem {

namespace {

[[nodiscard]] bool isPowerOfTwo(std::size_t v) noexcept {
    return v != 0 && (v & (v - 1)) == 0;
}

/// Cada alocação carrega um cabeçalho de 8 bytes imediatamente antes do
/// ponteiro do usuário, guardando o tamanho original — é o que permite
/// `reallocate` copiar blocos antigos com segurança.
constexpr std::size_t kHeaderSize = sizeof(std::size_t);

[[nodiscard]] std::size_t readHeader(const void* userPtr) noexcept {
    std::size_t size = 0;
    std::memcpy(&size, static_cast<const unsigned char*>(userPtr) - kHeaderSize, kHeaderSize);
    return size;
}

void writeHeader(void* userPtr, std::size_t size) noexcept {
    std::memcpy(static_cast<unsigned char*>(userPtr) - kHeaderSize, &size, kHeaderSize);
}

} // namespace

ArenaAllocator::ArenaAllocator(std::size_t capacityBytes, Allocator& backing,
                               const char* name) noexcept
    : name_(name), backing_(backing) {
    base_ = backing_.allocate(capacityBytes, sizeof(void*) * 2);
    if (base_ != nullptr) {
        capacity_ = capacityBytes;
    }
}

ArenaAllocator::~ArenaAllocator() {
    if (base_ != nullptr) {
        backing_.deallocate(base_);
    }
}

void* ArenaAllocator::allocate(std::size_t size, std::size_t alignment) noexcept {
    if (base_ == nullptr || size == 0 || !isPowerOfTwo(alignment)) {
        return nullptr;
    }

    // Alinhamento efetivo nunca menor que o do cabeçalho (8 bytes).
    const std::size_t effAlign = alignment < kHeaderSize ? kHeaderSize : alignment;
    if (effAlign > capacity_) {
        return nullptr; // impossível satisfazer neste bloco
    }

    // Posição desejada do ponteiro do usuário: alinhada e com cabeçalho antes.
    if (offset_ > capacity_ || kHeaderSize > capacity_ - offset_) {
        return nullptr; // sem espaço nem para o cabeçalho
    }
    const std::size_t afterHeader = offset_ + kHeaderSize;
    const std::size_t desired = (afterHeader + effAlign - 1) & ~(effAlign - 1);
    if (desired > capacity_ || size > capacity_ - desired) {
        return nullptr; // esgotado (checagens ordenadas evitam overflow)
    }

    void* ptr = static_cast<char*>(base_) + desired;
    writeHeader(ptr, size);
    lastPtr_ = ptr;
    lastStart_ = desired;                 // posição do usuário (cabeçalho em desired-8)
    lastSize_ = size;
    offset_ = desired + size;
    ++allocationCount_;
    return ptr;
}

void ArenaAllocator::deallocate(void* ptr) noexcept {
    if (ptr == nullptr || ptr != lastPtr_) {
        return; // arena: só a última alocação pode voltar (rewind de 1 nível)
    }
    offset_ = lastStart_ - kHeaderSize; // recupera cabeçalho + bloco
    lastPtr_ = nullptr;
    lastStart_ = 0;
    lastSize_ = 0;
    if (allocationCount_ > 0) {
        --allocationCount_;
    }
}

void* ArenaAllocator::reallocate(void* ptr, std::size_t newSize,
                                 std::size_t alignment) noexcept {
    if (ptr == nullptr) {
        return allocate(newSize, alignment);
    }
    if (newSize == 0) {
        deallocate(ptr);
        return nullptr;
    }
    if (!owns(ptr)) {
        return nullptr; // ponteiro fora do bloco desta arena
    }

    if (ptr != lastPtr_) {
        // Alocação antiga: copia para um bloco novo; a antiga fica órfã até o
        // reset (política de arena documentada no header).
        void* fresh = allocate(newSize, alignment);
        if (fresh == nullptr) {
            return nullptr; // bloco original permanece válido
        }
        const std::size_t oldSize = readHeader(ptr);
        const std::size_t bytesToCopy = oldSize < newSize ? oldSize : newSize;
        std::memcpy(fresh, ptr, bytesToCopy);
        return fresh;
    }

    // Última alocação: tenta estender in-place.
    if (lastStart_ + newSize <= capacity_) {
        writeHeader(ptr, newSize);
        offset_ = lastStart_ + newSize;
        lastSize_ = newSize;
        return ptr;
    }

    void* fresh = allocate(newSize, alignment);
    if (fresh == nullptr) {
        return nullptr;
    }
    std::memcpy(fresh, ptr, lastSize_);
    return fresh;
}

bool ArenaAllocator::owns(const void* ptr) const noexcept {
    if (ptr == nullptr || base_ == nullptr) {
        return false;
    }
    const char* p = static_cast<const char*>(ptr);
    const char* base = static_cast<const char*>(base_);
    return p >= base && p < base + capacity_;
}

const char* ArenaAllocator::name() const noexcept {
    return name_;
}

void ArenaAllocator::reset() noexcept {
    offset_ = 0;
    lastPtr_ = nullptr;
    lastStart_ = 0;
    lastSize_ = 0;
    allocationCount_ = 0;
}

ArenaAllocator::Stats ArenaAllocator::stats() const noexcept {
    return Stats{capacity_, offset_, allocationCount_};
}

} // namespace eng::mem
