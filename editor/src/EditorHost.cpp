#include "eng/editor/EditorHost.hpp"

/// EditorHost — host Android do editor (FASE 8). Espelha o AndroidRuntime
/// (FASE 7) com o MESMO contrato de surface/lifecycle (ADR-039/040), mas
/// renderiza o viewport do EditorDocument.

#include <cstdlib>
#include <cstring>
#include <string_view>
#include <utility>

#include "eng/audio/Audio.hpp"
#include "eng/log/Macros.hpp"
#include "eng/rhi/Renderer.hpp"
#include "eng/rhi/gles/GlesBackend.hpp"
#include "eng/rhi/vulkan/VulkanBackend.hpp"

#ifdef __ANDROID__
// Caminho RELATIVO a este TU: o include dir de android/runtime/src é do
// alvo goni (FASE 7), não do eng_editor — o caminho explícito vale nos
// dois builds sem acoplar CMake (no Linux o preprocessor remove).
#include <android/native_window.h>
#include "../../android/runtime/src/LogcatSink.hpp"
#endif

namespace eng::editor {

namespace {

ENG_LOG_CATEGORY("editor");

/// Registro de fábricas UMA vez por processo (idempotente — ADR-036).
void registerBackendFactories()
{
    static const bool registered = [] {
        using eng::rhi::BackendType;
        (void)eng::rhi::Renderer::registerBackend(
            BackendType::Vulkan, &eng::rhi::vulkan::createBackend);
        (void)eng::rhi::Renderer::registerBackend(
            BackendType::OpenGLES, &eng::rhi::gles::createBackend);
        return true;
    }();
    (void)registered;
}

[[nodiscard]] eng::rhi::BackendType backendFromName(const char* name)
{
    if (name == nullptr) {
        return eng::rhi::BackendType::Auto;
    }
    if (std::strcmp(name, "vulkan") == 0) {
        return eng::rhi::BackendType::Vulkan;
    }
    if (std::strcmp(name, "gles") == 0) {
        return eng::rhi::BackendType::OpenGLES;
    }
    return eng::rhi::BackendType::Auto;
}

}  // namespace

// =============================================================================
// Criação/destruição
// =============================================================================

eng::core::Result<EditorHost*> EditorHost::create(const char* backend,
                                                  const char* workspaceRootCStr)
{
    registerBackendFactories();
#ifdef __ANDROID__
    // Reuso do sink da FASE 7 (android/runtime/src/LogcatSink.hpp — no APK
    // ambos os módulos compilam no mesmo alvo goni; no Linux o include é
    // eliminado pelo preprocessor).
    static const bool logcatAttached = [] {
        static eng::android::LogcatSink sink;
        eng::log::Logger::get().addSink(sink);
        return true;
    }();
    (void)logcatAttached;
#endif

    EditorHost* host = new EditorHost{};
    // FRONTEIRA DO WORKSPACE (RECOVERY P0 — bug do APK: "destino absoluto
    // é proibido" / "caminho absoluto proibido"): o root físico (absoluto
    // no Android) é absorvido AQUI, no RootedFileSystem. O documento vê
    // apenas paths RELATIVOS ao workspace — as validações anti-absoluto do
    // editor continuam intactas (nada foi enfraquecido; a conversão que
    // faltava na arquitetura foi adicionada na fronteira certa).
    const eng::fs::Path workspaceRoot{
        workspaceRootCStr == nullptr ? std::string_view(".")
                                    : std::string_view(workspaceRootCStr)};
    host->rooted_ = std::make_unique<eng::fs::RootedFileSystem>(
        host->fs_, workspaceRoot);
    auto document = EditorDocument::create(*host->rooted_, eng::fs::Path{"."});
    if (document.isError()) {
        delete host;
        return eng::core::makeUnexpected(document.error());
    }
    host->requested_ = backendFromName(backend);
    host->document_ = std::move(document.value());
    ENG_INFO("Editor host criado (backend '{}', workspace root '{}')",
             backend == nullptr ? "auto" : backend, workspaceRoot.str());
    return host;
}

EditorHost::~EditorHost()
{
    stopAudio();  // P2 §12: device audio morre ANTES do mixer (documento)
    destroyRendererAndWindow();
}

// =============================================================================
// Surface (ADR-040 — contrato idêntico ao AndroidRuntime)
// =============================================================================

void EditorHost::acquireWindow(void* window) noexcept
{
#ifdef __ANDROID__
    if (window != nullptr) {
        ANativeWindow_acquire(static_cast<ANativeWindow*>(window));
    }
#else
    (void)window; // Linux: ponteiro-marker (testes) — sem NDK
#endif
}

void EditorHost::releaseWindow(void* window) noexcept
{
#ifdef __ANDROID__
    if (window != nullptr) {
        ANativeWindow_release(static_cast<ANativeWindow*>(window));
    }
#else
    (void)window;
#endif
}

void EditorHost::destroyRendererAndWindow() noexcept
{
    // Texturas GPU MORREM ANTES do renderer (handles pertencem ao renderer
    // vivo — ADR-035); o cache fica vazio para o próximo renderer.
    if (eng::rhi::Renderer* renderer =
            viewportRenderer_.has_value() ? viewportRenderer_->renderer() : nullptr;
        renderer != nullptr) {
        textureCache_.clear(*renderer);
    } else {
        textureCache_.discardAll();
    }
    // Renderer PRIMEIRO (backend libera a VkSurfaceKHR/EGLSurface antes da
    // janela morrer — ADR-040); depois o release da referência própria.
    viewportRenderer_.reset();
    if (window_ != nullptr) {
        releaseWindow(window_);
        window_ = nullptr;
    }
    windowKind_ = eng::rhi::NativeWindowKind::None;
}

void EditorHost::invalidateTextureCache()
{
    if (eng::rhi::Renderer* renderer =
            viewportRenderer_.has_value() ? viewportRenderer_->renderer() : nullptr;
        renderer != nullptr) {
        textureCache_.clear(*renderer);
    } else {
        textureCache_.discardAll();
    }
}

void EditorHost::surfaceCreated(void* window,
                                eng::rhi::NativeWindowKind kind,
                                std::uint32_t width, std::uint32_t height)
{
    if (window_ != nullptr) {
        // surfaceCreated duplicado (defensivo): descarta o anterior.
        destroyRendererAndWindow();
    }
    acquireWindow(window);
    window_ = window;
    windowKind_ = kind;
    pendingWidth_ = width;
    pendingHeight_ = height;

    if (width > 0 && height > 0 && createRendererForWindow(width, height)) {
        state_ = HostSurfaceState::Available;
    } else {
        // Tamanho desconhecido (surfaceChanged trará) — janela retida.
        state_ = HostSurfaceState::NoSurface;
    }
    ++stats_.surfaceCreations;
    ENG_INFO("Editor surface created ({}x{})", width, height);
}

void EditorHost::surfaceChanged(std::uint32_t width, std::uint32_t height)
{
    if (state_ == HostSurfaceState::Available ||
        state_ == HostSurfaceState::ChangedPending) {
        pendingWidth_ = width;
        pendingHeight_ = height;
        state_ = HostSurfaceState::ChangedPending; // aplica no próximo frame
    } else if (state_ == HostSurfaceState::NoSurface && window_ != nullptr &&
               width > 0 && height > 0) {
        // surfaceChanged chegou sem tamanho na created — cria agora.
        pendingWidth_ = width;
        pendingHeight_ = height;
        if (createRendererForWindow(width, height)) {
            state_ = HostSurfaceState::Available;
        }
    }
}

void EditorHost::surfaceDestroyed()
{
    destroyRendererAndWindow();
    state_ = HostSurfaceState::Destroyed;
    ++stats_.surfaceDestructions;
    ENG_INFO("Editor surface destroyed");
}

// =============================================================================
// Lifecycle
// =============================================================================

void EditorHost::onPause()
{
    paused_ = true; // flag apenas — robusto a qualquer ordem (§VI FASE 7)
    audioMixerLifecycle("onPause");  // P2 §12: vozes pausam com o app
}

void EditorHost::onResume()
{
    paused_ = false;
    (void)startAudio();  // P2 §12: religa o device após pausa
}

bool EditorHost::startAudio()
{
    if (document_ == nullptr) {
        return false;
    }
    if (audioBackend_ != nullptr && audioBackend_->isRunning()) {
        return true;  // idempotente
    }
    audioBackend_ = eng::audio::createDefaultBackend();
    if (audioBackend_ == nullptr) {
        return false;
    }
    auto started = audioBackend_->start(document_->audioMixer());
    if (started.isError()) {
        ENG_WARN("audio backend: {} — previews/Play continuam sem device",
                 started.error().message);
        audioBackend_.reset();
        return false;
    }
    ENG_INFO("audio backend '{}' ativo", audioBackend_->name());
    return true;
}

void EditorHost::stopAudio() noexcept
{
    if (audioBackend_ != nullptr) {
        audioBackend_->stop();
        audioBackend_.reset();
    }
    if (document_ != nullptr) {
        document_->audioMixer().stopAll();
    }
}

void EditorHost::audioMixerLifecycle(const char* reason) noexcept
{
    if (document_ == nullptr) {
        return;
    }
    if (std::string_view(reason) == "onPause") {
        document_->audioMixer().pauseAll();
        if (audioBackend_ != nullptr) {
            audioBackend_->stop();
        }
    } else {
        document_->audioMixer().resumeAll();
    }
}

void EditorHost::setBackend(const char* backend)
{
    requested_ = backendFromName(backend);
    if (state_ == HostSurfaceState::Available ||
        state_ == HostSurfaceState::ChangedPending) {
        // Recria AGORA com o novo backend (mesma janela).
        const std::uint32_t w = pendingWidth_;
        const std::uint32_t h = pendingHeight_;
        viewportRenderer_.reset();
        if (createRendererForWindow(w, h)) {
            state_ = HostSurfaceState::Available;
            ENG_INFO("backend trocado a quente");
        } else {
            state_ = HostSurfaceState::NoSurface;
        }
    }
    // Sem surface: aplica na próxima surfaceCreated (§XV FASE 7).
}

// =============================================================================
// Frame
// =============================================================================

bool EditorHost::createRendererForWindow(std::uint32_t width,
                                         std::uint32_t height)
{
    if (window_ == nullptr) {
        return false;
    }
    eng::rhi::SurfaceDesc surface;
    surface.window = eng::rhi::NativeWindowHandle{window_, windowKind_};
    surface.width = width;
    surface.height = height;
    auto renderer = ViewportRenderer::create(surface, requested_);
    if (renderer.isError()) {
        ENG_ERROR("viewport renderer não criado: {}", renderer.error().message);
        return false;
    }
    viewportRenderer_ = std::move(renderer.value());
    document_->viewport().setScreenSize(static_cast<float>(width),
                                       static_cast<float>(height));
    logSelection();
    return true;
}

void EditorHost::logSelection() const noexcept
{
    const auto* caps =
        viewportRenderer_.has_value() ? viewportRenderer_->capabilities()
                                      : nullptr;
    if (caps == nullptr) {
        return;
    }
    ENG_INFO("Backend selected: {}", caps->backendName);
    ENG_INFO("API version: {}", caps->apiVersion);
    ENG_INFO("GPU vendor: {}", caps->device.vendor);
    ENG_INFO("GPU renderer: {}", caps->device.name);
    ENG_INFO("Driver: {}", caps->device.driver);
    ENG_INFO("Software rendering: {}", caps->softwareRendering ? "yes" : "no");
}

bool EditorHost::renderFrame(float deltaSeconds)
{
    if (paused_ || state_ == HostSurfaceState::NoSurface ||
        state_ == HostSurfaceState::Destroyed ||
        !viewportRenderer_.has_value()) {
        ++stats_.framesSkippedNoSurface;
        return false;
    }

    if (state_ == HostSurfaceState::ChangedPending && window_ != nullptr) {
        auto resized =
            viewportRenderer_->resize(pendingWidth_, pendingHeight_);
        if (resized.isError()) {
            ++stats_.framesSkippedNoSurface;
            return false;
        }
        document_->viewport().setScreenSize(
            static_cast<float>(pendingWidth_),
            static_cast<float>(pendingHeight_));
        state_ = HostSurfaceState::Available;
    }

    // 1) tick do runtime (Play) — FASE 8: contrato; FASES 9/10 preenchem.
    document_->tick(deltaSeconds);

    // 2) render do foco (edição em Edit; clone em Play — §8.7). Sprites
    // com textura real via TextureCache (evolução P0-3 — o documento é a
    // fonte dos dados; o host é o dono do renderer/upload). Sem projeto →
    // assets nulos: sprites caem no caminho de cor (honesto). Gizmo P1:
    // desenhado por cima (tool ativa + seleção; Edit apenas).
    const eng::scene::Scene* scene = document_->sceneInFocus();
    const auto quads = document_->viewport().buildQuads(
        *scene, document_->selection());
    const auto particles = document_->viewport().buildParticleQuads(*scene);
    gizmoDraw_ = document_->gizmoDraw(&textureCache_);
    const bool drew = viewportRenderer_->renderFrame(
        document_->viewport(), quads, particles, document_->isPlaying(),
        document_->assets(), textureCache_, &gizmoDraw_);

    if (drew) {
        ++stats_.framesSubmitted;
        stats_.framesPresented = viewportRenderer_->framesPresented();
        stats_.firstFrameSubmitted = stats_.firstFrameSubmitted ||
                                     viewportRenderer_->framesSubmitted() > 0;
        stats_.firstFramePresented = stats_.firstFramePresented ||
                                     viewportRenderer_->framesPresented() > 0;
    }
    return drew;
}

eng::rhi::BackendType EditorHost::selectedBackend() const noexcept
{
    return viewportRenderer_.has_value() ? viewportRenderer_->activeBackend()
                                        : eng::rhi::BackendType::Auto;
}

const eng::rhi::RendererCapabilities* EditorHost::capabilities() const noexcept
{
    return viewportRenderer_.has_value() ? viewportRenderer_->capabilities()
                                        : nullptr;
}

}  // namespace eng::editor
