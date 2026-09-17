/// Testes do runtime Android no LINUX (FASE 7, missão §XXVIII/§XXIX).
///
/// O state machine + ownership + demo rodam CONTRA OS BACKENDS REAIS
/// (lavapipe/llvmpipe) com surface Headless — os mesmos fontes que o APK
/// compila para arm64-v8a (o que muda no Android é o caminho
/// ANativeWindow→surface, compilado sob __ANDROID__ e exercitado no APK).
///
/// Ciclos exigidos (§XXVIII/§XXIX): CREATE→RENDER→DESTROY→RECREATE→RENDER;
/// RUNNING→PAUSE→RESUME→RUNNING; ordens inusuais (§VI); shutdown (§XX).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "eng/android/AndroidRuntime.hpp"
#include "eng/log/ConsoleSink.hpp"
#include "eng/log/Logger.hpp"
#include "eng/rhi/Renderer.hpp"

namespace {

/// Logs do engine no stderr durante os testes (eventos-chave e erros dos
/// backends — essenciais para diagnosticar falhas de ambiente).
struct LogSetup {
    eng::log::ConsoleSink console{stderr};
    LogSetup() { eng::log::Logger::get().addSink(console); }
};
const LogSetup kLogSetup{};

using eng::android::AndroidRuntime;
using eng::android::SurfaceState;
using eng::rhi::NativeWindowKind;

constexpr std::uint32_t kWidth = 64;
constexpr std::uint32_t kHeight = 48;

/// Cria runtime com backend auto (herda seleção ADR-036) e surface
/// headless "de mentira" (ponteiro marker — no Linux não há NDK).
struct RuntimeWithSurface {
    AndroidRuntime* runtime{nullptr};
    int marker{0};

    RuntimeWithSurface(const char* backend = "auto") {
        auto created = AndroidRuntime::create(backend);
        REQUIRE(created.ok());
        runtime = created.value();
        runtime->surfaceCreated(&marker, NativeWindowKind::Headless, kWidth, kHeight);
    }
    ~RuntimeWithSurface() { delete runtime; }
};

}  // namespace

// =============================================================================
// Criação e seleção
// =============================================================================

TEST_CASE("android: runtime cria com backends reais registrados", "[android_runtime]")
{
    auto created = AndroidRuntime::create("auto");
    REQUIRE(created.ok());
    AndroidRuntime* runtime = created.value();
    CHECK(runtime->state() == SurfaceState::NoSurface);
    CHECK_FALSE(runtime->paused());
    CHECK(runtime->stats().framesSubmitted == 0);
    CHECK(runtime->selectedBackend() == eng::rhi::BackendType::Auto);
    delete runtime;
}

TEST_CASE("android: NO_SURFACE nunca renderiza nem crasha", "[android_runtime]")
{
    auto created = AndroidRuntime::create("auto");
    REQUIRE(created.ok());
    AndroidRuntime* runtime = created.value();

    // renderFrame em qualquer combinação pré-surface: no-op seguro (§VIII).
    CHECK_FALSE(runtime->renderFrame());
    runtime->onResume();
    CHECK_FALSE(runtime->renderFrame());
    // Com pause ativo o skip é contabilizado como paused (checado antes).
    runtime->onPause();
    CHECK_FALSE(runtime->renderFrame());
    // surfaceChanged sem surface: no-op (§VI).
    runtime->surfaceChanged(32, 32);
    CHECK(runtime->state() == SurfaceState::NoSurface);
    CHECK(runtime->stats().framesSkippedNoSurface == 2);
    CHECK(runtime->stats().framesSkippedPaused == 1);

    // shutdown limpo sem surface.
    delete runtime;
}

// =============================================================================
// CREATE → RENDER → DESTROY → RECREATE → RENDER (§XXVIII — crítico)
// =============================================================================

TEST_CASE("android: surface create → render → destroy → recreate → render",
          "[android_runtime]")
{
    RuntimeWithSurface env;
    AndroidRuntime* runtime = env.runtime;
    REQUIRE(runtime->state() == SurfaceState::Available);
    REQUIRE(runtime->capabilities() != nullptr);
    CHECK(runtime->capabilities()->presentation);

    // CREATE → RENDER (frame REAL com backend real — lavapipe/llvmpipe).
    REQUIRE(runtime->renderFrame());
    CHECK(runtime->stats().firstFrameSubmitted);
    CHECK(runtime->stats().firstFramePresented);
    CHECK(runtime->stats().framesSubmitted == 1);
    REQUIRE(runtime->renderFrame());
    CHECK(runtime->stats().framesSubmitted == 2);

    // DESTROY → estado NO_SURFACE/DESTROYED, sem render.
    runtime->surfaceDestroyed();
    CHECK(runtime->state() == SurfaceState::Destroyed);
    CHECK_FALSE(runtime->renderFrame());
    CHECK(runtime->stats().framesSubmitted == 2);  // nada novo
    CHECK(runtime->stats().surfaceDestructions == 1);

    // RECREATE → RENDER de novo (renderer NOVO sobre a MESMA janela marker).
    runtime->surfaceCreated(&env.marker, NativeWindowKind::Headless, kWidth, kHeight);
    REQUIRE(runtime->state() == SurfaceState::Available);
    REQUIRE(runtime->renderFrame());
    CHECK(runtime->stats().framesSubmitted == 3);
    CHECK(runtime->stats().surfaceCreations == 2);

    // Janela NOVA (recreation total): liberar a antiga e seguir renderizando.
    int other = 0;
    runtime->surfaceCreated(&other, NativeWindowKind::Headless, kWidth, kHeight);
    CHECK(runtime->state() == SurfaceState::Available);
    REQUIRE(runtime->renderFrame());
    CHECK(runtime->stats().framesSubmitted == 4);
}

TEST_CASE("android: surfaceChanged aplica resize no próximo frame (§XXVIII)",
          "[android_runtime]")
{
    RuntimeWithSurface env;
    AndroidRuntime* runtime = env.runtime;
    REQUIRE(runtime->renderFrame());

    runtime->surfaceChanged(128, 96);
    CHECK(runtime->state() == SurfaceState::ChangedPending);
    // O próximo frame aplica o resize (backend recria swapchain/pbuffer)
    // e renderiza.
    REQUIRE(runtime->renderFrame());
    CHECK(runtime->state() == SurfaceState::Available);
    CHECK(runtime->stats().framesSubmitted == 2);
}

// =============================================================================
// PAUSE / RESUME (§XXIX)
// =============================================================================

TEST_CASE("android: RUNNING → PAUSE → RESUME → RUNNING", "[android_runtime]")
{
    RuntimeWithSurface env;
    AndroidRuntime* runtime = env.runtime;
    REQUIRE(runtime->renderFrame());

    runtime->onPause();
    CHECK(runtime->paused());
    CHECK_FALSE(runtime->renderFrame());  // sem trabalho gráfico (§VIII)
    CHECK(runtime->stats().framesSkippedPaused == 1);
    CHECK(runtime->stats().framesSubmitted == 1);  // nada novo

    // resize durante pause: aplicado, mas sem render.
    runtime->surfaceChanged(32, 32);

    runtime->onResume();
    CHECK_FALSE(runtime->paused());
    // Após resume: o frame aplica o resize pendente e volta a renderizar —
    // o renderer NÃO se perdeu (missão §XXIX).
    REQUIRE(runtime->renderFrame());
    CHECK(runtime->state() == SurfaceState::Available);
    CHECK(runtime->stats().framesSubmitted == 2);
}

TEST_CASE("android: pause/resume sem surface e ordens inusuais (§VI)", "[android_runtime]")
{
    auto created = AndroidRuntime::create("auto");
    REQUIRE(created.ok());
    AndroidRuntime* runtime = created.value();

    // pause sem surface; resume depois; surface por último.
    runtime->onPause();
    runtime->onResume();
    runtime->surfaceCreated(nullptr, NativeWindowKind::Headless, kWidth, kHeight);
    CHECK(runtime->state() == SurfaceState::NoSurface);  // janela nula ignorada

    int marker = 0;
    runtime->surfaceCreated(&marker, NativeWindowKind::Headless, kWidth, kHeight);
    CHECK(runtime->state() == SurfaceState::Available);
    REQUIRE(runtime->renderFrame());

    // surfaceDestroyed antes do pause (§VI): destrói; pause tardio: no-op.
    runtime->surfaceDestroyed();
    runtime->onPause();
    CHECK(runtime->state() == SurfaceState::Destroyed);
    CHECK_FALSE(runtime->renderFrame());
    runtime->onResume();
    CHECK_FALSE(runtime->renderFrame());

    // surfaceDestroyed duplicado/tardio: no-op defensivo.
    runtime->surfaceDestroyed();
    CHECK(runtime->state() == SurfaceState::Destroyed);

    delete runtime;
}

// =============================================================================
// Backend selection (§XIV/XV) e troca
// =============================================================================

TEST_CASE("android: seleção auto escolhe Vulkan; setBackend recria AGORA", "[android_runtime]")
{
    RuntimeWithSurface env;
    AndroidRuntime* runtime = env.runtime;
    REQUIRE(runtime->state() == SurfaceState::Available);
    REQUIRE(runtime->renderFrame());

    const auto* caps = runtime->capabilities();
    REQUIRE(caps != nullptr);
    CHECK_FALSE(caps->backendName.empty());
    CHECK(runtime->stats().firstFramePresented);

    // Troca com surface viva: renderer recriado imediatamente com GLES.
    runtime->setBackend("gles");
    CHECK(runtime->selectedBackend() == eng::rhi::BackendType::OpenGLES);
    REQUIRE(runtime->state() == SurfaceState::Available);
    REQUIRE(runtime->capabilities() != nullptr);
    CHECK(runtime->capabilities()->backendName == "OpenGL ES");
    // E volta a renderizar com o novo backend (mesma janela).
    REQUIRE(runtime->renderFrame());

    // String não reconhecida → Auto; o renderer é recriado e o Auto escolhe
    // o melhor backend disponível (Vulkan no ambiente de teste).
    runtime->setBackend("banana");
    CHECK(runtime->selectedBackend() == eng::rhi::BackendType::Vulkan);
    REQUIRE(runtime->renderFrame());
}

TEST_CASE("android: backend explícito indisponível mantém runtime vivo", "[android_runtime]")
{
    auto created = AndroidRuntime::create("gles");
    REQUIRE(created.ok());
    AndroidRuntime* runtime = created.value();

    int marker = 0;
    runtime->surfaceCreated(&marker, NativeWindowKind::Headless, kWidth, kHeight);
    if (runtime->state() != SurfaceState::Available) {
        // GLES indisponível neste ambiente (sem libEGL): runtime permanece
        // NO_SURFACE — SEM crash, recriável (seleção explícita sem fallback
        // — missão §XIV).
        CHECK(runtime->state() == SurfaceState::NoSurface);
        CHECK(runtime->capabilities() == nullptr);
    } else {
        REQUIRE(runtime->renderFrame());
    }
    delete runtime;
}

// =============================================================================
// Shutdown (§XX)
// =============================================================================

TEST_CASE("android: surfaceCreated sem tamanho cria renderer no primeiro change",
          "[android_runtime]")
{
    // Fluxo REAL do Android (missão §VI): surfaceCreated não conhece o
    // tamanho; surfaceChanged entrega w/h antes de qualquer render.
    auto created = AndroidRuntime::create("auto");
    REQUIRE(created.ok());
    AndroidRuntime* runtime = created.value();

    int marker = 0;
    runtime->surfaceCreated(&marker, NativeWindowKind::Headless, 0, 0);
    CHECK(runtime->state() == SurfaceState::Available);
    CHECK(runtime->capabilities() == nullptr);  // renderer adiado
    CHECK_FALSE(runtime->renderFrame());  // sem renderer: no-op seguro

    runtime->surfaceChanged(kWidth, kHeight);
    CHECK(runtime->state() == SurfaceState::Available);
    REQUIRE(runtime->capabilities() != nullptr);
    REQUIRE(runtime->renderFrame());
    CHECK(runtime->stats().firstFramePresented);

    // Troca subsequente de tamanho segue o caminho ChangedPending normal.
    runtime->surfaceChanged(32, 32);
    CHECK(runtime->state() == SurfaceState::ChangedPending);
    REQUIRE(runtime->renderFrame());
    CHECK(runtime->state() == SurfaceState::Available);

    // Destroy com renderer vivo encerra limpo.
    runtime->surfaceDestroyed();
    CHECK(runtime->state() == SurfaceState::Destroyed);
    delete runtime;
}

TEST_CASE("android: shutdown com surface viva e recursos em uso", "[android_runtime]")
{
    AndroidRuntime* runtime = nullptr;
    {
        RuntimeWithSurface env;
        REQUIRE(env.runtime->renderFrame());
        runtime = env.runtime;
        env.runtime = nullptr;  // o dtor do holder não deve deletar
    }
    // dtor do runtime com renderer+demo+janela vivos: ordem renderer→janela,
    // sem leak (ASan/LSan ativos no preset debug — gate implícito).
    delete runtime;
}
