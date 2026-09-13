#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <cstdint>

#include "eng/mem/Memory.hpp"

namespace {

using eng::mem::HeapAllocator;

} // namespace

TEST_CASE("HeapAllocator aloca, escreve e lê memória", "[mem][heap]") {
    HeapAllocator heap;
    REQUIRE(heap.name() != nullptr);

    int* values = static_cast<int*>(heap.allocate(sizeof(int) * 4, 16));
    REQUIRE(values != nullptr);

    values[0] = 10;
    values[1] = 20;
    values[2] = 30;
    values[3] = 40;
    CHECK(values[3] == 40);
    CHECK(values[0] + values[1] + values[2] == 60);

    heap.deallocate(values);
    CHECK_FALSE(heap.owns(values));
}

TEST_CASE("HeapAllocator respeita alinhamentos arbitrários", "[mem][heap]") {
    HeapAllocator heap;

    void* a64 = heap.allocate(64, 64);
    REQUIRE(a64 != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(a64) % 64 == 0);

    void* a4096 = heap.allocate(128, 4096);
    REQUIRE(a4096 != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(a4096) % 4096 == 0);

    heap.deallocate(a64);
    heap.deallocate(a4096);
}

TEST_CASE("HeapAllocator rejeita pedidos inválidos", "[mem][heap]") {
    HeapAllocator heap;
    CHECK(heap.allocate(0, 16) == nullptr);            // tamanho zero
    CHECK(heap.allocate(16, 3) == nullptr);            // alinhamento não é potência de 2
    CHECK(heap.allocate(16, 0) == nullptr);            // alinhamento zero
    heap.deallocate(nullptr);                          // não deve explodir
}

TEST_CASE("HeapAllocator rastreia owns apenas de ponteiros vivos", "[mem][heap]") {
    HeapAllocator heap;
    int foreign = 0;

    void* p = heap.allocate(32, 16);
    REQUIRE(p != nullptr);
    CHECK(heap.owns(p));
    CHECK_FALSE(heap.owns(&foreign));
    CHECK_FALSE(heap.owns(nullptr));

    heap.deallocate(p);
    CHECK_FALSE(heap.owns(p));
}

TEST_CASE("HeapAllocator reallocate preserva conteúdo", "[mem][heap]") {
    HeapAllocator heap;

    char* text = static_cast<char*>(heap.allocate(16, 16));
    REQUIRE(text != nullptr);
    std::memcpy(text, "0123456789ABCD", 15);

    char* grown = static_cast<char*>(heap.reallocate(text, 64, 16));
    REQUIRE(grown != nullptr);
    CHECK(std::memcmp(grown, "0123456789ABCD", 15) == 0);
    CHECK_FALSE(heap.owns(text)); // antigo foi liberado
    CHECK(heap.owns(grown));

    // Encolher também preserva o prefixo.
    char* shrunk = static_cast<char*>(heap.reallocate(grown, 8, 16));
    REQUIRE(shrunk != nullptr);
    CHECK(std::memcmp(shrunk, "01234567", 8) == 0);

    heap.deallocate(shrunk);
}

TEST_CASE("HeapAllocator reallocate trata nulo e zero", "[mem][heap]") {
    HeapAllocator heap;

    void* fresh = heap.reallocate(nullptr, 32, 16);
    CHECK(fresh != nullptr);
    CHECK(heap.owns(fresh));

    void* gone = heap.reallocate(fresh, 0, 16);
    CHECK(gone == nullptr);
    CHECK_FALSE(heap.owns(fresh));
}

TEST_CASE("HeapAllocator estatísticas acompanham o ciclo de vida", "[mem][heap]") {
    HeapAllocator heap;
    CHECK(heap.stats().activeAllocations == 0);
    CHECK(heap.stats().activeBytes == 0);
    CHECK(heap.stats().totalAllocations == 0);

    void* a = heap.allocate(100, 16);
    void* b = heap.allocate(200, 16);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    CHECK(heap.stats().activeAllocations == 2);
    CHECK(heap.stats().activeBytes == 300);
    CHECK(heap.stats().totalAllocations == 2);
    CHECK(heap.stats().peakAllocations == 2);

    heap.deallocate(a);
    CHECK(heap.stats().activeAllocations == 1);
    CHECK(heap.stats().activeBytes == 200);
    CHECK(heap.stats().totalAllocations == 2); // total é cumulativo

    heap.deallocate(b);
    CHECK(heap.stats().activeAllocations == 0);
    CHECK(heap.hasLeaks() == false);
}

TEST_CASE("HeapAllocator devolve ponteiros distintos", "[mem][heap]") {
    HeapAllocator heap;
    void* ptrs[8] = {};
    for (void*& p : ptrs) {
        p = heap.allocate(64, 16);
        REQUIRE(p != nullptr);
    }
    for (std::size_t i = 0; i < 8; ++i) {
        for (std::size_t j = i + 1; j < 8; ++j) {
            CHECK(ptrs[i] != ptrs[j]);
        }
    }
    for (void* p : ptrs) {
        heap.deallocate(p);
    }
}

TEST_CASE("HeapAllocator reallocate de ponteiro estranho devolve nulo", "[mem][heap]") {
    HeapAllocator heapA;
    HeapAllocator heapB;

    void* p = heapA.allocate(32, 16);
    REQUIRE(p != nullptr);
    CHECK(heapB.reallocate(p, 64, 16) == nullptr); // não pertence a heapB

    heapA.deallocate(p);
}
