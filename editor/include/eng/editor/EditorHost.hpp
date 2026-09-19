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
#include <memory>
#include <optional>
#include <string_view>

#include "eng/audio/Audio.hpp"
#include "eng/core/Result.hpp"
#include "eng/editor/EditorDocument.hpp"
#include "eng/editor/TextureCache.hpp"
#include "eng/editor/ViewportRenderer.hpp"
#include "eng/fs/NativeFileSystem.hpp"
#include "eng/fs/RootedFileSystem.hpp"
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
    /// P3.1: primeiro frame APRESENTADO marcado como STARTUP_COMPLETE no
    /// diagnóstico persistente (uma vez por sessão do host).
    bool startupComplete{false};
};

class EditorHost final {
public:
    /// Registra as fábricas de backend (Vulkan+GLES reais — feito UMA vez,
    /// idempotente) e cria o documento com workspace no `workspaceRoot`.
    /// O Renderer só nasce quando a surface chega (§8.7/ADR-039).
    ///
    /// FRONTEIRA DO WORKSPACE (RECOVERY P0): `workspaceRoot` é o local
    /// FÍSICO — absoluto no Android (filesDir/projects) ou relativo no
    /// Linux. Ele NUNCA entra no documento: é absorvido pelo
    /// RootedFileSystem AQUI, e o editor inteiro opera com paths
    /// RELATIVOS ao workspace (contrato §8.1). É o que separa a
    /// representação de armazenamento do Android (SAF/filesDir) da
    /// abstração de projeto-relativo da engine.
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

    /// ÁUDIO do editor (P2 §12): liga o mixer do documento ao backend
    /// da plataforma (AAudio no Android — real device output; null no
    /// Linux/testes). Idempotente; onResume religa se necessário.
    [[nodiscard]] bool startAudio();
    void stopAudio() noexcept;
    [[nodiscard]] bool audioRunning() const noexcept
    {
        return audioBackend_ != nullptr && audioBackend_->isRunning();
    }

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

    /// Cache de texturas do host (upload GPU sob demanda — evolução P0-3).
    [[nodiscard]] TextureCache& textureCache() noexcept { return textureCache_; }

    /// Filesystem NATIVO do host (absoluto/CWD — NÃO é o que o documento
    /// vê; use apenas para I/O de plataforma fora do workspace do editor).
    [[nodiscard]] eng::fs::NativeFileSystem& fileSystem() noexcept { return fs_; }

    /// Filesystem do WORKSPACE (rooted — paths relativos ao workspaceRoot;
    /// absoluto é rejeitado na fronteira). É o fs que o documento usa;
    /// exposto para staging de import (o SAF copia para DENTRO do
    /// workspace) e testes.
    [[nodiscard]] eng::fs::FileSystem& workspace() noexcept { return *rooted_; }

    /// ViewportRenderer ativo (diagnóstico/testes; nullptr sem surface).
    [[nodiscard]] const ViewportRenderer* viewportRenderer() const noexcept
    {
        return viewportRenderer_.has_value() ? &viewportRenderer_.value() : nullptr;
    }
    /// Não-const — readback de pixels em testes visuais (RECOVERY P0).
    [[nodiscard]] ViewportRenderer* viewportRenderer() noexcept
    {
        return viewportRenderer_.has_value() ? &viewportRenderer_.value() : nullptr;
    }

    /// Invalida o cache de texturas (troca de projeto/reimport — chamado
    /// pela fronteira JNI nos comandos que mudam assets/textures).
    void invalidateTextureCache();

    /// Política de projeto na inicialização (P3 §0 — bug Android
    /// "AlreadyExists"): repassa ao documento. Ver EditorDocument::
    /// ensureStartupProject() para a semântica completa.
    [[nodiscard]] eng::core::Result<std::string> ensureStartupProject();

    /// Estado completo do host para diagnóstico (logcat [GONI] — P3 §0:
    /// "se o bug voltar, logcat indica operação/projeto/caminho/estado/
    /// origem"). Uma linha por campo, estável para grep.
    void dumpState(const char* origin) const noexcept;

private:
    EditorHost() = default;

    void destroyRendererAndWindow() noexcept;
    [[nodiscard]] bool createRendererForWindow(std::uint32_t width,
                                               std::uint32_t height);
    void logSelection() const noexcept;
    /// pauseAll/stop vs resumeAll do mixer (onPause/onResume — P2 §12).
    void audioMixerLifecycle(const char* reason) noexcept;

    // --- watchdog de backend (P3 §0 — "fecha rapidamente" no Android) -------
    //
    // Sessões que morrem ANTES de apresentar kWatchdogHealthyFrames deixam
    // um marcador "trying:<backend>" na raiz do workspace. A próxima
    // sessão Auto PULA o backend que morreu (alternância explícita no
    // log); após um início saudável o marcador vira "good:<backend>" e o
    // Auto passa a PREFERIR o backend comprovadamente estável. Escolha
    // EXPLÍCITA do usuário nunca é desfeita pelo watchdog.
    [[nodiscard]] std::string watchdogRead() const noexcept;
    void watchdogWrite(std::string_view state) noexcept;
    void watchdogOnRendererCreated() noexcept;
    void watchdogOnFramePresented() noexcept;
    /// Backend pedido (Auto) ajustado pelo watchdog: pula o backend que
    /// morreu na sessão anterior / prefere o comprovadamente bom.
    [[nodiscard]] eng::rhi::BackendType effectiveBackend() const noexcept;

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

    eng::fs::NativeFileSystem fs_{};  ///< dono do I/O real (base física)
    /// Mapeamento workspace (RECOVERY P0): conversão absoluto↔relativo.
    /// Declaração ANTES de document_ (destruição na ordem inversa:
    /// documento morre primeiro — ele empresta o rooted, que empresta fs_).
    std::unique_ptr<eng::fs::RootedFileSystem> rooted_{};
    eng::rhi::BackendType requested_{eng::rhi::BackendType::Auto};
    void* window_{nullptr};
    eng::rhi::NativeWindowKind windowKind_{eng::rhi::NativeWindowKind::None};
    HostSurfaceState state_{HostSurfaceState::NoSurface};
    bool paused_{false};
    std::uint32_t pendingWidth_{0};
    std::uint32_t pendingHeight_{0};

    std::unique_ptr<EditorDocument> document_{};
    std::optional<ViewportRenderer> viewportRenderer_{};
    TextureCache textureCache_{};  ///< texturas GPU por nome de asset (P0-3)
    GizmoDrawData gizmoDraw_{};   ///< geometria do gizmo do frame (P1)
    /// Backend de áudio (P2 §12): AAudio no Android, null no Linux. O
    /// mixer vive no DOCUMENTO (vozes do Play + previews) — o backend
    /// apenas PUXA o mix na thread própria do device.
    std::unique_ptr<eng::audio::IAudioBackend> audioBackend_{};
    HostStats stats_{};
    /// Watchdog (P3 §0): frames já apresentados desde a (re)criação do
    /// renderer — usado p/ promover "trying:X" → "good:X".
    std::uint64_t watchdogBaseline_{0};
    /// Último estado lido do watchdog ("trying:X"/"good:X"/vazio) — cache
    /// da decisão de startup (não relê o disco por frame). Mutable:
    /// memoização lida por effectiveBackend() const.
    mutable std::string watchdogState_{};
};

}  // namespace eng::editor
