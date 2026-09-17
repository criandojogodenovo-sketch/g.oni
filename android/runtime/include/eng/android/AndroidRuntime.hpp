#pragma once

/// eng::android — runtime Android sobre a abstraction (FASE 7, missão §IV–§IX).
///
/// Consumidor de engine/: TODO JNI fica em GoniJni.cpp (único arquivo com
/// jni.h — missão §II); este módulo é C++ puro, testável no Linux com
/// backends reais (lavapipe/llvmpipe) e os MESMOS fontes compilam no APK
/// (arm64-v8a) via CMake do NDK.
///
/// Estados de surface (missão §IV) — independentes do lifecycle paused:
///   NO_SURFACE → SURFACE_AVAILABLE → (SURFACE_CHANGED pendente)* →
///   SURFACE_DESTROYED → NO_SURFACE...
/// A ordem real dos callbacks pode variar (missão §VI): toda transição é
/// tolerada; o runtime nunca renderiza sem surface e nunca mantém um
/// ANativeWindow além do seu lifetime (ADR-040).

#include <cstdint>
#include <optional>
#include <string_view>

#include "eng/core/Error.hpp"
#include "eng/core/Result.hpp"
#include "eng/rhi/Renderer.hpp"
#include "eng/rhi/Types.hpp"

namespace eng::android {

enum class SurfaceState : std::uint8_t {
    NoSurface,       ///< inicial / pós-destroy — NUNCA renderiza (missão §VIII)
    Available,       ///< renderer vivo e utilizable
    ChangedPending,  ///< resize recebido — aplicado no próximo renderFrame
    Destroyed,       ///< janela liberada — aguarda NO_SURFACE (defensivo)
};

/// Diagnóstico p/ testes/logcat (valores REAIS dos backends — §XVIII).
struct RuntimeStats {
    std::uint64_t framesSubmitted{0};
    std::uint64_t framesPresented{0};
    std::uint64_t framesSkippedNoSurface{0};
    std::uint64_t framesSkippedPaused{0};
    std::uint32_t surfaceCreations{0};
    std::uint32_t surfaceDestructions{0};
    bool firstFrameSubmitted{false};
    bool firstFramePresented{false};
};

class AndroidRuntime {
public:
    /// Cria o runtime com backend pedido ("auto"/"vulkan"/"gles" — §XV).
    /// Registra backends Vulkan+GLES (fábricas reais) e o logcat sink (no
    /// Android). O Renderer só nasce quando a surface chega (missão §VIII).
    [[nodiscard]] static eng::core::Result<AndroidRuntime*> create(const char* backend);

    /// Libera TUDO: renderer (recursos via backend — ADR-035), demo,
    /// referências de fábrica e a janela (se ainda viva — defensivo).
    ~AndroidRuntime();

    AndroidRuntime(const AndroidRuntime&) = delete;
    AndroidRuntime& operator=(const AndroidRuntime&) = delete;

    // --- surface (missão §V/§XXVIII) ---------------------------------------

    /// Janela nativa disponível. `window` é o ANativeWindow* JÁ ADQUIRIDO
    /// pelo chamador (JNI) — o runtime assume a ownership dele até
    /// surfaceDestroyed() (ADR-040). Em testes Linux: ponteiro-marker com
    /// kind Headless (sem acquire/release — não há NDK).
    void surfaceCreated(void* window,
                        eng::rhi::NativeWindowKind kind = defaultWindowKind(),
                        std::uint32_t width = 64, std::uint32_t height = 48);

    /// Dimensão mudou (mesma janela, tipicamente). Aplicado no próximo
    /// renderFrame (resize do backend recria swapchain/pbuffer).
    void surfaceChanged(std::uint32_t width, std::uint32_t height);

    /// Janela indo embora: destrói renderer+demo PRIMEIRO, então libera a
    /// janela — o renderer nunca toca um ANativeWindow morto (§V).
    void surfaceDestroyed();

    // --- lifecycle (missão §VI/§XXIX) --------------------------------------

    void onPause();   ///< apenas flag: sem trabalho gráfico (§VIII)
    void onResume();  ///< idem — robusto em qualquer ordem

    /// Troca o backend desejado ("auto"/"vulkan"/"gles"). Se houver surface
    /// ativa, o renderer é recriado AGORA com o novo backend (logado);
    /// caso contrário aplica-se na próxima surfaceCreated (§XV).
    void setBackend(const char* backend);

    // --- frame (missão §VIII/§XVI) -----------------------------------------

    /// Um frame do demo: begin → clear → pipeline → vbo → draw(3) → end →
    /// present. Retorna false quando não desenhou (NO_SURFACE/DESTROYED/
    /// paused/minimized/out-of-date persistente — NUNCA é erro lançar).
    bool renderFrame();

    // --- introspecção (testes + logcat) -------------------------------------

    [[nodiscard]] SurfaceState state() const noexcept { return state_; }
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    [[nodiscard]] const RuntimeStats& stats() const noexcept { return stats_; }
    /// Backend selecionado (válido após surfaceCreated com sucesso).
    [[nodiscard]] eng::rhi::BackendType selectedBackend() const noexcept;
    [[nodiscard]] const eng::rhi::RendererCapabilities* capabilities() const noexcept;

private:
    AndroidRuntime() = default;

    struct Demo {
        /// Renderer é move-only SEM ctor default (criação só via
        /// Renderer::create) — optional segura a instância viva.
        std::optional<eng::rhi::Renderer> renderer{};
        eng::rhi::BufferHandle vertexBuffer{};
        eng::rhi::ShaderHandle shader{};
        eng::rhi::GraphicsPipelineHandle pipeline{};
    };

    void destroyRendererAndWindow() noexcept;
    [[nodiscard]] bool createRendererForWindow(std::uint32_t width, std::uint32_t height);
    void logSelection() const noexcept;

    static void releaseWindow(void* window) noexcept;
    static void acquireWindow(void* window) noexcept;
    [[nodiscard]] static constexpr eng::rhi::NativeWindowKind defaultWindowKind() noexcept {
#ifdef __ANDROID__
        return eng::rhi::NativeWindowKind::Android;
#else
        return eng::rhi::NativeWindowKind::Headless;
#endif
    }

    eng::rhi::BackendType requested_{eng::rhi::BackendType::Auto};
    void* window_{nullptr};  ///< ANativeWindow* (owner entre created/destroyed)
    eng::rhi::NativeWindowKind windowKind_{eng::rhi::NativeWindowKind::None};
    SurfaceState state_{SurfaceState::NoSurface};
    bool paused_{false};
    std::uint32_t pendingWidth_{0};
    std::uint32_t pendingHeight_{0};
    Demo demo_{};
    RuntimeStats stats_{};
};

}  // namespace eng::android
