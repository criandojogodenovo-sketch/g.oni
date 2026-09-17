#pragma once

/// eng::editor::EditorHost — runtime Android do EDITOR (FASE 8).
///
/// Espelha a arquitetura do `eng::android::AndroidRuntime` (FASE 7,
/// ADR-039/040) com o mesmo contrato de estados/ownership — mas renderiza
/// o VIEWPORT do EditorDocument (não o demo):
///
///   Kotlin EditorActivity (Choreographer, UI thread)
///        ↓ JNI (EditorJni.cpp — único TU com jni.h do editor)
///   EditorHost: surface/lifecycle + EditorDocument + ViewportRenderer
///        ↓
///   eng::scene/serial/project/assets + eng::rhi → Vulkan/GLES
///
/// Estados de surface: idênticos ao AndroidRuntime (NoSurface → Available →
/// ChangedPending → Destroyed; paused independente; nunca renderiza sem
/// surface; renderer morre ANTES da janela — ADR-040).
///
/// jni.h NÃO aparece aqui — a fronteira vive em android/app (EditorJni.cpp);
/// este módulo é C++ puro, testável no Linux com backends reais.

#include <cstdint>
#include <optional>
#include <string_view>

#include "eng/core/Result.hpp"
#include "eng/editor/EditorDocument.hpp"
#include "eng/editor/ViewportRenderer.hpp"
#include "eng/fs/NativeFileSystem.hpp"
#include "eng/rhi/Types.hpp"

namespace eng::editor {

enum class HostSurfaceState : std::uint8_t {
    NoSurface,
    Available,
    ChangedPending,
    Destroyed,
};

/// Diagnóstico (testes/logcat — mesmos campos do AndroidRuntime, FASE 7).
struct HostStats {
    std::uint64_t framesSubmitted{0};
    std::uint64_t framesPresented{0};
    std::uint64_t framesSkippedNoSurface{0};
    std::uint64_t framesSkippedPaused{0};
    std::uint32_t surfaceCreations{0};
    std::uint32_t surfaceDestructions{0};
    bool firstFrameSubmitted{false};
    bool firstFramePresented{false};
};

class EditorHost final {
public:
    /// Registra as fábricas de backend (Vulkan+GLES reais — feito UMA vez,
    /// idempotente) e cria o documento com workspace no `workspaceRoot`.
    /// O Renderer só nasce quando a surface chega (§8.7/ADR-039).
    [[nodiscard]] static eng::core::Result<EditorHost*> create(
        const char* backend, const char* workspaceRoot);

    ~EditorHost();
    EditorHost(const EditorHost&) = delete;
    EditorHost& operator=(const EditorHost&) = delete;

    // --- surface (mesmo contrato do AndroidRuntime — ADR-040) ---------------

    void surfaceCreated(void* window,
                        eng::rhi::NativeWindowKind kind = defaultWindowKind(),
                        std::uint32_t width = 64, std::uint32_t height = 48);
    void surfaceChanged(std::uint32_t width, std::uint32_t height);
    void surfaceDestroyed();

    // --- lifecycle -----------------------------------------------------------

    void onPause();
    void onResume();
    void setBackend(const char* backend);

    // --- frame (Choreographer) -----------------------------------------------

    /// tick do runtime (Play) + render do viewport (foco do modo).
    /// false quando não desenhou (sem surface/paused/minimized).
    bool renderFrame(float deltaSeconds);

    // --- acesso ao documento (JNI opera por aqui) ------------------------------

    [[nodiscard]] EditorDocument& document() noexcept { return *document_; }
    [[nodiscard]] const EditorDocument& document() const noexcept
    {
        return *document_;
    }

    [[nodiscard]] HostSurfaceState state() const noexcept { return state_; }
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    [[nodiscard]] const HostStats& stats() const noexcept { return stats_; }
    [[nodiscard]] eng::rhi::BackendType selectedBackend() const noexcept;
    [[nodiscard]] const eng::rhi::RendererCapabilities* capabilities() const
        noexcept;

private:
    EditorHost() = default;

    void destroyRendererAndWindow() noexcept;
    [[nodiscard]] bool createRendererForWindow(std::uint32_t width,
                                               std::uint32_t height);
    void logSelection() const noexcept;

    static void releaseWindow(void* window) noexcept;
    static void acquireWindow(void* window) noexcept;
    [[nodiscard]] static constexpr eng::rhi::NativeWindowKind
    defaultWindowKind() noexcept
    {
#ifdef __ANDROID__
        return eng::rhi::NativeWindowKind::Android;
#else
        return eng::rhi::NativeWindowKind::Headless;
#endif
    }

    eng::fs::NativeFileSystem fs_{};  ///< dono do I/O (documento empresta)
    eng::rhi::BackendType requested_{eng::rhi::BackendType::Auto};
    void* window_{nullptr};
    eng::rhi::NativeWindowKind windowKind_{eng::rhi::NativeWindowKind::None};
    HostSurfaceState state_{HostSurfaceState::NoSurface};
    bool paused_{false};
    std::uint32_t pendingWidth_{0};
    std::uint32_t pendingHeight_{0};

    std::unique_ptr<EditorDocument> document_{};
    std::optional<ViewportRenderer> viewportRenderer_{};
    HostStats stats_{};
};

}  // namespace eng::editor
