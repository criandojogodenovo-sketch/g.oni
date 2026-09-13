#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>

#include "eng/mem/Memory.hpp"

namespace {

using eng::mem::ArenaAllocator;
using eng::mem::HeapAllocator;

} // namespace

TEST_CASE("ArenaAllocator aloca com alinhamento e rastreia owns", "[mem][arena]") {
    HeapAllocator heap;
    ArenaAllocator arena(256, heap);

    void* p = arena.allocate(64, 16);
    REQUIRE(p != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(p) % 16 == 0);
    CHECK(arena.owns(p));
    CHECK(arena.stats().capacity == 256);
    CHECK(arena.stats().allocationCount == 1);
    CHECK(arena.stats().usedBytes > 64); // inclui cabeçalho de 8 bytes

    int foreign = 0;
    CHECK_FALSE(arena.owns(&foreign));
    CHECK_FALSE(arena.owns(nullptr));
}

TEST_CASE("ArenaAllocator esgota a capacidade devolvendo nulo", "[mem][arena]") {
    HeapAllocator heap;
    ArenaAllocator arena(128, heap);

    void* a = arena.allocate(64, 16);
    REQUIRE(a != nullptr);
    // Primeira: header+64 → offset 80. A segunda precisaria de 96+64=160 > 128.
    CHECK(arena.allocate(64, 16) == nullptr);
}

TEST_CASE("ArenaAllocator rejeita pedidos inválidos", "[mem][arena]") {
    HeapAllocator heap;
    ArenaAllocator arena(128, heap);

    CHECK(arena.allocate(0, 16) == nullptr);
    CHECK(arena.allocate(16, 3) == nullptr); // não é potência de 2
}

TEST_CASE("ArenaAllocator reset devolve todo o bloco", "[mem][arena]") {
    HeapAllocator heap;
    ArenaAllocator arena(256, heap);

    void* a = arena.allocate(100, 16);
    void* b = arena.allocate(100, 16);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(arena.stats().allocationCount == 2);

    arena.reset();
    CHECK(arena.stats().allocationCount == 0);
    CHECK(arena.stats().usedBytes == 0);

    // Após reset, o espaço total volta a estar disponível.
    void* big = arena.allocate(240, 16);
    CHECK(big != nullptr);
}

TEST_CASE("ArenaAllocator dealloc libera apenas a última alocação", "[mem][arena]") {
    HeapAllocator heap;
    ArenaAllocator arena(256, heap);

    void* a = arena.allocate(32, 16);
    void* b = arena.allocate(32, 16);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    const std::size_t usedAfterB = arena.stats().usedBytes;

    // Liberar alocação antiga é no-op.
    arena.deallocate(a);
    CHECK(arena.stats().usedBytes == usedAfterB);

    // Liberar a última devolve o espaço do bloco + cabeçalho.
    arena.deallocate(b);
    CHECK(arena.stats().usedBytes < usedAfterB);
    CHECK(arena.stats().allocationCount == 1);

    // Nova alocação reutiliza o espaço da última liberada.
    void* again = arena.allocate(32, 16);
    REQUIRE(again != nullptr);
    CHECK(again == b);
}

TEST_CASE("ArenaAllocator reallocate estende a última in-place", "[mem][arena]") {
    HeapAllocator heap;
    ArenaAllocator arena(256, heap);

    char* a = static_cast<char*>(arena.allocate(64, 16));
    REQUIRE(a != nullptr);
    std::memcpy(a, "dados", 6);

    char* same = static_cast<char*>(arena.reallocate(a, 128, 16));
    REQUIRE(same != nullptr);
    CHECK(same == a); // mesma posição
    CHECK(std::memcmp(same, "dados", 6) == 0);
    CHECK(arena.stats().usedBytes >= 128 + 16); // header + 128 bytes
}

TEST_CASE("ArenaAllocator reallocate copia blocos não-últimos", "[mem][arena]") {
    HeapAllocator heap;
    ArenaAllocator arena(512, heap);

    char* a = static_cast<char*>(arena.allocate(32, 16));
    char* b = static_cast<char*>(arena.allocate(32, 16));
    char* c = static_cast<char*>(arena.allocate(32, 16));
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    std::memcpy(b, "payload-1234", 13);

    char* moved = static_cast<char*>(arena.reallocate(b, 96, 16));
    REQUIRE(moved != nullptr);
    CHECK(moved != b); // bloco novo, pois b não é a última
    CHECK(std::memcmp(moved, "payload-1234", 13) == 0);
}

TEST_CASE("ArenaAllocator reallocate falha sem corromper o original", "[mem][arena]") {
    HeapAllocator heap;
    ArenaAllocator arena(64, heap);

    char* a = static_cast<char*>(arena.allocate(32, 16));
    REQUIRE(a != nullptr);
    std::memcpy(a, "ok", 3);

    // Última + não cabe in-place + sem espaço para bloco novo → nulo.
    CHECK(arena.reallocate(a, 512, 16) == nullptr);
    CHECK(std::memcmp(a, "ok", 3) == 0);
    CHECK(arena.owns(a));
}

TEST_CASE("ArenaAllocator reallocate trata nulo e zero", "[mem][arena]") {
    HeapAllocator heap;
    ArenaAllocator arena(128, heap);

    void* fresh = arena.reallocate(nullptr, 32, 16);
    CHECK(fresh != nullptr);
    CHECK(arena.owns(fresh));

    CHECK(arena.reallocate(fresh, 0, 16) == nullptr);
    CHECK(arena.stats().allocationCount == 0);
}

TEST_CASE("ArenaAllocator devolve o bloco ao alocador de backing no destrutor", "[mem][arena]") {
    HeapAllocator heap;
    {
        ArenaAllocator arena(128, heap);
        REQUIRE(arena.allocate(32, 16) != nullptr);
    } // arena destruída aqui

    // O bloco voltou para o heap — nenhuma alocação viva restou.
    CHECK(heap.stats().activeAllocations == 0);
    CHECK(heap.hasLeaks() == false);
}

TEST_CASE("ArenaAllocator de capacidade zero fica inerte", "[mem][arena]") {
    HeapAllocator heap;
    ArenaAllocator arena(0, heap);
    CHECK(arena.allocate(16, 16) == nullptr);
    CHECK(arena.stats().capacity == 0);
}
