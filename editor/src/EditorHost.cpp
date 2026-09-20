#include "eng/editor/EditorHost.hpp"

#include "eng/editor/Diagnostics.hpp"

/// EditorHost — host Android do editor (FASE 8). Espelha o AndroidRuntime
/// (FASE 7) com o MESMO contrato de surface/lifecycle (ADR-039/040), mas
/// renderiza o viewport do EditorDocument.

#include <cstdlib>
#include <cstring>
#include <string>
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

[[nodiscard]] const char* backendToName(eng::rhi::BackendType type) noexcept
{
    switch (type) {
    case eng::rhi::BackendType::Vulkan: return "vulkan";
    case eng::rhi::BackendType::OpenGLES: return "gles";
    case eng::rhi::BackendType::Auto: break;
    }
    return "auto";
}

/// Frames apresentados para declarar uma sessão SAUDÁVEL (promove o
/// backend "trying" → "good"). ~0.5s a 60Hz: suficiente p/ atravessar a
/// criação de surface + primeiros draws (onde ocorre a morte súbita).
constexpr std::uint64_t kWatchdogHealthyFrames = 30;
/// Arquivo do watchdog na RAIZ do workspace (oculto, fora dos projetos).
constexpr std::string_view kWatchdogFile{".goni_backend_watchdog"};

/// P3.4 — trampoline do hook de progresso do backend de áudio: os
/// estágios granulares (backend_stage::*) viram marcos do diagnóstico
/// persistido 1:1 (mesma UI thread de start(); o mirror P3.2 copia
/// para Download/GONI a cada estágio — a janela de morte do C33 fica
/// cercada estágio a estágio). Sem userdata: diag é global do processo.
void audioBackendProgress(void* /*userdata*/, const char* stage,
                           const char* status, const char* detail)
{
    eng::editor::diag::mark(stage, status, detail);
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
    diag::mark("STARTUP_EDITOR_HOST", "begin");
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
    diag::mark("STARTUP_FILESYSTEM", "ok", workspaceRoot.str().c_str());
    auto document = EditorDocument::create(*host->rooted_, eng::fs::Path{"."});
    if (document.isError()) {
        diag::mark("STARTUP_EDITOR_DOCUMENT", "failed",
                   document.error().message.c_str());
        delete host;
        return eng::core::makeUnexpected(document.error());
    }
    diag::mark("STARTUP_EDITOR_DOCUMENT", "ok");
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
    diag::mark("STARTUP_SURFACE", "acquired",
               "referência própria do ANativeWindow");

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
    diag::mark("STARTUP_RESUME", "begin");
    paused_ = false;
    (void)startAudio();  // P2 §12: religa o device após pausa
    diag::mark("STARTUP_RESUME", "ok");
}

bool EditorHost::startAudio()
{
    // P3.4 — granular: cercar TODO o caminho de áudio com estágios
    // persistidos (o open do AAudio envolve dlopen + binder + HAL do
    // dispositivo — as chamadas de sistema mais opacas da janela de
    // morte súbita do C33). O backend emite os seus próprios marcos
    // (AUDIO_*) via hook — instalado ANTES de criar o backend.
    if (document_ == nullptr) {
        return false;
    }
    if (audioBackend_ != nullptr && audioBackend_->isRunning()) {
        return true;  // idempotente
    }
    diag::mark("STARTUP_AUDIO", "begin");
    eng::audio::setBackendProgressHook(&audioBackendProgress, nullptr);
    audioBackend_ = eng::audio::createDefaultBackend();
    if (audioBackend_ == nullptr) {
        diag::mark("STARTUP_AUDIO", "failed", "createDefaultBackend = null");
        return false;
    }
    {
        const std::string backendName{audioBackend_->name()};
        diag::mark("STARTUP_AUDIO", "backend", backendName.c_str());
    }
    audioFirstFrameMarked_ = false;
    auto started = audioBackend_->start(document_->audioMixer());
    if (started.isError()) {
        ENG_WARN("audio backend: {} — previews/Play continuam sem device",
                 started.error().message);
        diag::mark("STARTUP_AUDIO", "failed",
                   started.error().message.c_str());
        audioBackend_.reset();
        return false;
    }
    {
        const std::string device = audioBackend_->describeDevice();
        diag::mark("STARTUP_AUDIO", "started", device.c_str());
    }
    ENG_INFO("audio backend '{}' ativo", audioBackend_->name());
    // P3.4: o primeiro callback pode disparar já DENTRO do requestStart
    // (o AAudio começa a puxar antes de retornar) — observa agora.
    checkAudioFirstCallbackFrame();
    return true;
}

void EditorHost::stopAudio() noexcept
{
    if (audioBackend_ != nullptr) {
        audioBackend_->stop();
        audioBackend_.reset();
    }
    audioFirstFrameMarked_ = false;
    if (document_ != nullptr) {
        document_->audioMixer().stopAll();
    }
}

void EditorHost::checkAudioFirstCallbackFrame()
{
    // P3.4 — evidência de vida do pull: o callback do AAudio marca um
    // átomo na própria thread de áudio; persistimos o marco UMA vez,
    // da UI thread (o hook/mirror do diagnóstico nunca roda na thread
    // de áudio — reentrância proibida por contrato).
    if (audioFirstFrameMarked_ || audioBackend_ == nullptr ||
        !audioBackend_->isRunning() ||
        !audioBackend_->hasFirstCallbackFired()) {
        return;
    }
    audioFirstFrameMarked_ = true;
    const std::string device = audioBackend_->describeDevice();
    diag::mark(eng::audio::backend_stage::CallbackFirstFrame, "ok",
               device.c_str());
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
    // Watchdog (P3 §0): Auto é ajustado pelo histórico da SESSÃO
    // anterior (backend que morreu é pulado; comprovadamente bom é
    // preferido). Escolha explícita do usuário passa intacta.
    const eng::rhi::BackendType effective = effectiveBackend();
    if (effective != requested_) {
        ENG_WARN(
            "watchdog: Auto ajustado {} -> {} (sessão anterior deixou "
            "'{}')",
            backendToName(requested_), backendToName(effective),
            watchdogState_.empty() ? "-" : watchdogState_);
    }
    diag::mark("STARTUP_SURFACE", "renderer",
               backendToName(effective));
    auto renderer = ViewportRenderer::create(surface, effective);
    if (renderer.isError()) {
        ENG_ERROR("viewport renderer não criado: {}", renderer.error().message);
        return false;
    }
    viewportRenderer_ = std::move(renderer.value());
    document_->viewport().setScreenSize(static_cast<float>(width),
                                       static_cast<float>(height));
    watchdogOnRendererCreated();
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
    // P3.4 — observa o primeiro callback de áudio mesmo em frames
    // pulados (sem surface/paused): a evidência não depende do render.
    if (!audioFirstFrameMarked_) {
        checkAudioFirstCallbackFrame();
    }

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
    auto quads = document_->viewport().buildQuads(*scene,
                                                   document_->selection());
    // P3 §3: material do sprite → shader/tint do quad (cache do documento;
    // sem projeto/default os quads já saem "lit" com tint intacto).
    document_->resolveMaterials(quads);
    const auto particles = document_->viewport().buildParticleQuads(*scene);
    gizmoDraw_ = document_->gizmoDraw(&textureCache_);
    const bool drew = viewportRenderer_->renderFrame(
        document_->viewport(), quads, particles, document_->isPlaying(),
        document_->assets(), textureCache_, &gizmoDraw_);

    if (drew) {
        if (!stats_.startupComplete) {
            stats_.startupComplete = true;
            diag::mark("STARTUP_COMPLETE", "ok", "first frame presented");
        }
        ++stats_.framesSubmitted;
        stats_.framesPresented = viewportRenderer_->framesPresented();
        watchdogOnFramePresented();  // P3 §0: promove trying → good
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

// =============================================================================
// Startup/diagnóstico (P3 §0 — bug Android "AlreadyExists" + "fecha
// rapidamente")
// =============================================================================

eng::core::Result<std::string> EditorHost::ensureStartupProject()
{
    // Origem registrada: a Activity chama UM ponto (nativeEditorEnsureProject)
    // — a política inteira (listar/decidir/criar/abrir) vive no documento,
    // testável no Linux sem Android.
    auto opened = document_->ensureStartupProject();
    if (opened.isError()) {
        diag::mark("STARTUP_PROJECT", "failed",
                   opened.error().message.c_str());
    } else {
        diag::mark("STARTUP_PROJECT", "ok", opened.value().c_str());
        // STARTUP_MATERIAL: o sistema de materiais inicializa junto ao
        // projeto (assets/materials + cache de resolução — P3 §3).
        diag::mark("STARTUP_MATERIAL", "ok", opened.value().c_str());
    }
    return opened;
}

void EditorHost::dumpState(const char* origin) const noexcept
{
    // Formato estável "state: <campo> = <valor>" — grep-ável no logcat.
    ENG_INFO("state: origem = {}", origin == nullptr ? "-" : origin);
    ENG_INFO("state: backend pedido = {}", backendToName(requested_));
    ENG_INFO("state: backend ativo = {}",
             viewportRenderer_.has_value()
                 ? backendToName(viewportRenderer_->activeBackend())
                 : "-");
    ENG_INFO("state: surface = {}", [this] {
        switch (state_) {
        case HostSurfaceState::NoSurface: return "no_surface";
        case HostSurfaceState::Available: return "available";
        case HostSurfaceState::ChangedPending: return "changed_pending";
        case HostSurfaceState::Destroyed: return "destroyed";
        }
        return "?";
    }());
    ENG_INFO("state: frames submetidos = {} | apresentados = {}",
             stats_.framesSubmitted, stats_.framesPresented);
    ENG_INFO("state: paused = {} | watchdog = '{}'", paused_,
             watchdogState_.empty() ? "-" : watchdogState_);
    if (document_ == nullptr) {
        ENG_INFO("state: documento = <nulo>");
        return;
    }
    const EditorDocument& doc = *document_;
    ENG_INFO("state: documento.hasProject = {} | projeto = '{}'",
             doc.hasProject(), doc.projectName());
    ENG_INFO("state: documento.projectRoot = '{}'",
             doc.hasProject() ? doc.projectRoot().str() : "-");
    ENG_INFO("state: modo = {} | sceneDirty = {} | projectDirty = {}",
             doc.isPlaying() ? "play" : "edit", doc.sceneDirty(),
             doc.projectDirty());
}

// --- watchdog de backend ------------------------------------------------------

std::string EditorHost::watchdogRead() const noexcept
{
    if (rooted_ == nullptr) {
        return {};
    }
    auto text = rooted_->readAllText(
        eng::fs::Path{kWatchdogFile});
    if (text.isError()) {
        return {};
    }
    // Trim defensivo (mesma política do .goni_last_project).
    std::string_view state{text.value()};
    while (!state.empty() &&
           (state.front() == '\n' || state.front() == '\r' ||
            state.front() == ' ')) {
        state.remove_prefix(1);
    }
    while (!state.empty() &&
           (state.back() == '\n' || state.back() == '\r' ||
            state.back() == ' ')) {
        state.remove_suffix(1);
    }
    return std::string{state};
}

void EditorHost::watchdogWrite(std::string_view state) noexcept
{
    if (rooted_ == nullptr) {
        return;
    }
    // Best-effort por design: o watchdog NUNCA pode derrubar a sessão.
    (void)rooted_->writeAllText(eng::fs::Path{kWatchdogFile}, state);
}

void EditorHost::watchdogOnRendererCreated() noexcept
{
    if (viewportRenderer_.has_value()) {
        watchdogBaseline_ = viewportRenderer_->framesPresented();
        const char* name =
            backendToName(viewportRenderer_->activeBackend());
        watchdogWrite(std::string{"trying:"} + name);
        watchdogState_ = std::string{"trying:"} + name;
        ENG_INFO("watchdog: sessão iniciando com backend '{}' (marcador "
                 "'trying:{}')",
                 name, name);
    }
}

void EditorHost::watchdogOnFramePresented() noexcept
{
    if (!viewportRenderer_.has_value()) {
        return;
    }
    const std::uint64_t presented =
        viewportRenderer_->framesPresented() - watchdogBaseline_;
    if (presented < kWatchdogHealthyFrames) {
        return;
    }
    // Sessão saudável: o backend ATIVO sobreviveu — promove a "good".
    const char* name = backendToName(viewportRenderer_->activeBackend());
    const std::string good = std::string{"good:"} + name;
    if (watchdogState_ != good) {
        watchdogWrite(good);
        watchdogState_ = good;
        ENG_INFO("watchdog: backend '{}' saudável ({} frames) — marcador "
                 "'good:{}'",
                 name, presented, name);
    }
}

eng::rhi::BackendType EditorHost::effectiveBackend() const noexcept
{
    if (requested_ != eng::rhi::BackendType::Auto) {
        return requested_;  // escolha explícita: watchdog não mexe
    }
    watchdogState_ = watchdogRead();
    const std::string_view state = watchdogState_;
    if (state == "trying:vulkan") {
        // Sessão anterior com Vulkan morreu antes de 30 frames.
        return eng::rhi::BackendType::OpenGLES;
    }
    if (state == "trying:gles") {
        return eng::rhi::BackendType::Vulkan;
    }
    if (state == "good:gles") {
        return eng::rhi::BackendType::OpenGLES;
    }
    if (state == "good:vulkan") {
        return eng::rhi::BackendType::Vulkan;
    }
    return eng::rhi::BackendType::Auto;  // sem histórico: ordem padrão
}

}  // namespace eng::editor
