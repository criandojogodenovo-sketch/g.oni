#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "eng/jobs/JobSystem.hpp"

#if defined(ENG_JOBS_CATCH_EXCEPTIONS) && ENG_JOBS_CATCH_EXCEPTIONS != 0
#define ENG_JOBS_TESTS_CATCH 1
#else
#define ENG_JOBS_TESTS_CATCH 0
#endif

using eng::jobs::JobHandle;
using eng::jobs::JobSystem;

namespace {

constexpr int kStressJobs = 10000;

} // namespace

// =============================================================================
// Construção e ciclo de vida
// =============================================================================

TEST_CASE("jobs: workerCount default (hardware) e explícito", "[jobs]")
{
    JobSystem defaults;
    CHECK(defaults.workerCount() >= 1);

    JobSystem one(1);
    CHECK(one.workerCount() == 1);

    JobSystem three(3);
    CHECK(three.workerCount() == 3);
}

TEST_CASE("jobs: destrutor faz join — nenhum thread vaza", "[jobs]")
{
    // ASan/LSan detectaria threads vivos + estados pendentes após o escopo.
    {
        JobSystem system(4);
        (void)system.submit([] {});
        (void)system.submit([] {});
    } // ~JobSystem: drena e encerra
    CHECK(true);
}

// =============================================================================
// Execução e espera
// =============================================================================

TEST_CASE("jobs: único job executa e wait retorna", "[jobs]")
{
    JobSystem system(2);

    std::atomic<int> value{0};
    auto handle = system.submit([&value] { value.store(7, std::memory_order_release); });

    REQUIRE(handle.valid());
    handle.wait();
    CHECK(value.load(std::memory_order_acquire) == 7);
}

TEST_CASE("jobs: N jobs com handles individuais", "[jobs]")
{
    JobSystem system(4);

    constexpr int kJobs = 32;
    std::vector<std::atomic<int>> results(static_cast<std::size_t>(kJobs));
    std::vector<JobHandle> handles;

    for (int i = 0; i < kJobs; ++i) {
        handles.push_back(system.submit([i, &results] {
            results[static_cast<std::size_t>(i)].store(i + 1, std::memory_order_release);
        }));
    }
    for (auto& handle : handles) {
        handle.wait();
    }
    for (int i = 0; i < kJobs; ++i) {
        CHECK(results[static_cast<std::size_t>(i)].load(std::memory_order_acquire) == i + 1);
    }
}

TEST_CASE("jobs: wait bloqueia até a conclusão", "[jobs]")
{
    JobSystem system(1); // serial: determinístico

    std::atomic<bool> done{false};
    auto handle = system.submit([&done] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        done.store(true, std::memory_order_release);
    });

    handle.wait();
    CHECK(done.load(std::memory_order_acquire)); // só pode ser true APÓS wait
}

TEST_CASE("jobs: efeito do job é visível após wait (happens-before)", "[jobs]")
{
    JobSystem system(2);

    int plain = 0; // escrito pelo job, lido após wait — sem atomic
    auto handle = system.submit([&plain] { plain = 42; });
    handle.wait();
    CHECK(plain == 42);
}

TEST_CASE("jobs: stress — 10k jobs curtos via waitAll", "[jobs]")
{
    JobSystem system(2); // máquina de CI tem 2 núcleos

    std::atomic<int> counter{0};
    for (int i = 0; i < kStressJobs; ++i) {
        (void)system.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    system.waitAll();
    CHECK(counter.load(std::memory_order_acquire) == kStressJobs);
}

TEST_CASE("jobs: stress com oversubscription (4 workers, 2 núcleos)", "[jobs]")
{
    JobSystem system(4);

    std::atomic<int> counter{0};
    for (int i = 0; i < kStressJobs; ++i) {
        (void)system.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    system.waitAll();
    CHECK(counter.load(std::memory_order_acquire) == kStressJobs);
}

TEST_CASE("jobs: jobs que submetem filhos (cascata de 2 níveis) completam", "[jobs]")
{
    JobSystem system(2);

    std::atomic<int> parents{0};
    std::atomic<int> children{0};
    JobSystem* systemPtr = &system;
    constexpr int kParents = 100;

    for (int i = 0; i < kParents; ++i) {
        (void)system.submit([systemPtr, &parents, &children] {
            parents.fetch_add(1, std::memory_order_relaxed);
            // filho submetido DE DENTRO de um job — fila viva durante execução
            (void)systemPtr->submit([&children] {
                children.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::microseconds(20));
            });
        });
    }
    system.waitAll();
    CHECK(parents.load(std::memory_order_acquire) == kParents);
    CHECK(children.load(std::memory_order_acquire) == kParents);
}

// =============================================================================
// Shutdown
// =============================================================================

TEST_CASE("jobs: shutdown com jobs pendentes drena tudo", "[jobs]")
{
    std::atomic<int> executed{0};
    {
        JobSystem system(2);
        constexpr int kPending = 100;
        for (int i = 0; i < kPending; ++i) {
            (void)system.submit([&executed] {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                executed.fetch_add(1, std::memory_order_release);
            });
        }
        // SEM wait — o destrutor precisa drenar
    }
    CHECK(executed.load(std::memory_order_acquire) == 100);
}

TEST_CASE("jobs: submit após shutdown devolve handle inválido (sem crash)", "[jobs]")
{
    JobSystem system(2);
    system.shutdown();

    CHECK(system.isShutdown());
    auto handle = system.submit([] {});
    CHECK_FALSE(handle.valid());
    handle.wait();     // no-op
    system.waitAll();  // no-op

    system.shutdown(); // segunda chamada: no-op
    system.shutdown(); // terceira: idempotente
    // destrutor após shutdown explícito: no-op adicional
}

TEST_CASE("jobs: shutdown explícito idempotente + destrutor seguro", "[jobs]")
{
    JobSystem system(1);
    (void)system.submit([] {});
    system.shutdown();
    system.shutdown();
}

TEST_CASE("jobs: wait em handle de job já concluído funciona pós-destruição", "[jobs]")
{
    JobHandle handle;
    {
        JobSystem system(2);
        std::atomic<int> value{0};
        handle = system.submit([&value] { value.store(5, std::memory_order_release); });
        handle.wait();
        CHECK(value.load(std::memory_order_acquire) == 5);
    } // sistema destruído — o estado do job sobrevive (shared)

    REQUIRE(handle.valid());
    handle.wait(); // retorna imediatamente (done), sem crash/hang
}

// =============================================================================
// Handles
// =============================================================================

TEST_CASE("jobs: handle movido — origem perde o estado, destino espera", "[jobs]")
{
    JobSystem system(1);

    std::atomic<int> value{0};
    JobHandle source = system.submit([&value] { value.store(9, std::memory_order_release); });

    std::vector<JobHandle> owned;
    owned.push_back(std::move(source)); // movimento
    CHECK_FALSE(source.valid());

    owned.back().wait();
    CHECK(value.load(std::memory_order_acquire) == 9);
}

TEST_CASE("jobs: callable grande (acima do small-buffer) usa heap e executa", "[jobs]")
{
    JobSystem system(2);

    // 64 bytes de captura: excede o buffer de 48 → caminho heap.
    std::array<std::byte, 64> blob{};
    std::atomic<int> ran{0};

    auto handle = system.submit([blob, &ran] {
        (void)blob;
        ran.fetch_add(1, std::memory_order_release);
    });
    handle.wait();
    CHECK(ran.load(std::memory_order_acquire) == 1);
}

// =============================================================================
// Exceções (apenas quando ENG_JOBS_CATCH_EXCEPTIONS — release: terminate)
// =============================================================================

#if ENG_JOBS_TESTS_CATCH

TEST_CASE("jobs: exceção do job propaga via wait() com a mensagem original", "[jobs]")
{
    JobSystem system(1);

    auto handle = system.submit([] { throw std::runtime_error("boom-jobs"); });

    bool caught = false;
    try {
        handle.wait();
    } catch (const std::runtime_error& e) {
        caught = true;
        CHECK(std::string(e.what()) == "boom-jobs");
    }
    CHECK(caught);
}

TEST_CASE("jobs: pool sobrevive a job que lança — jobs seguintes executam", "[jobs]")
{
    JobSystem system(1); // serial: ordem determinística

    std::atomic<int> after{0};
    auto bad = system.submit([] { throw std::logic_error("bad"); });
    auto good = system.submit([&after] { after.fetch_add(1, std::memory_order_release); });

    bool caught = false;
    try {
        bad.wait();
    } catch (const std::logic_error&) {
        caught = true;
    }
    CHECK(caught);

    good.wait();
    CHECK(after.load(std::memory_order_acquire) == 1);

    // O pool continua aceitando e executando normalmente:
    std::atomic<int> more{0};
    auto extra = system.submit([&more] { more.fetch_add(1, std::memory_order_release); });
    extra.wait();
    CHECK(more.load(std::memory_order_acquire) == 1);
}

TEST_CASE("jobs: waitAll conclui mesmo com jobs que lançam", "[jobs]")
{
    JobSystem system(2);

    std::atomic<int> ok{0};
    for (int i = 0; i < 10; ++i) {
        (void)system.submit([i, &ok] {
            if (i % 2 == 0) {
                throw std::runtime_error("par");
            }
            ok.fetch_add(1, std::memory_order_relaxed);
        });
    }
    system.waitAll(); // não trava: exceções armazenadas, não propagadas aqui
    CHECK(ok.load(std::memory_order_acquire) == 5);
}

#else

TEST_CASE("jobs: build sem ENG_JOBS_CATCH_EXCEPTIONS — jobs normais verdes", "[jobs]")
{
    // Caminho do release: sem captura de exceções. Exceção em job encerra o
    // processo (std::terminate) — política documentada em ADR-023; não
    // testável sem matar o binário. Aqui só confirmamos execução normal.
    JobSystem system(1);
    std::atomic<int> value{0};
    auto handle = system.submit([&value] { value.store(1, std::memory_order_release); });
    handle.wait();
    CHECK(value.load(std::memory_order_acquire) == 1);
}

#endif
