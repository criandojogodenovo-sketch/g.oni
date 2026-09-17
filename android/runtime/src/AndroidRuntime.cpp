/// eng::android — implementação do runtime (FASE 7). Ver AndroidRuntime.hpp.
///
/// Render thread: a thread que chama renderFrame() (UI thread dirigida por
/// Choreographer no APK — ADR-039; a thread dos testes no Linux). O runtime
/// não cria threads próprias (missão §VII: a arquitetura mais simples
/// correta).

#include "eng/android/AndroidRuntime.hpp"

#include <utility>

#include "eng/log/Macros.hpp"
#include "eng/rhi/gles/GlesBackend.hpp"
#include "eng/rhi/vulkan/VulkanBackend.hpp"

#include "TriangleDemoShaders.hpp"

#ifdef __ANDROID__
#include <android/native_window.h>

#include "LogcatSink.hpp"
#endif

ENG_LOG_CATEGORY("rhi.android")

namespace eng::android {
namespace {

using eng::core::StatusCode;
using eng::rhi::BackendType;
using eng::rhi::FrameAcquireStatus;

[[nodiscard]] BackendType parseBackend(const char* backend) noexcept {
    if (backend == nullptr) {
        return BackendType::Auto;
    }
    if (std::string_view{backend} == "vulkan" || std::string_view{backend} == "VULKAN") {
        return BackendType::Vulkan;
    }
    if (std::string_view{backend} == "gles" || std::string_view{backend} == "opengles" ||
        std::string_view{backend} == "GLES") {
        return BackendType::OpenGLES;
    }
    return BackendType::Auto;
}

[[nodiscard]] std::string_view backendName(BackendType type) noexcept {
    switch (type) {
    case BackendType::Vulkan: return "Vulkan";
    case BackendType::OpenGLES: return "OpenGL ES";
    case BackendType::Auto: return "Auto";
    }
    return "?";
}

}  // namespace

// =============================================================================
// Criação / destruição
// =============================================================================

eng::core::Result<AndroidRuntime*> AndroidRuntime::create(const char* backend) {
    // Fábricas dos backends REAIS (nenhum fake — missão §XVI).
    if (auto registered = eng::rhi::Renderer::registerBackend(
            BackendType::Vulkan, &eng::rhi::vulkan::createBackend);
        !registered && registered.error().code != StatusCode::AlreadyExists) {
        return eng::core::makeUnexpected(registered.error());
    }
    if (auto registered = eng::rhi::Renderer::registerBackend(
            BackendType::OpenGLES, &eng::rhi::gles::createBackend);
        !registered && registered.error().code != StatusCode::AlreadyExists) {
        return eng::core::makeUnexpected(registered.error());
    }

#ifdef __ANDROID__
    static LogcatSink logcat{};  //.Logger global sobrevive ao runtime; sink idempotente
    eng::log::Logger::get().addSink(logcat);
#endif

    auto* runtime = new AndroidRuntime();
    runtime->requested_ = parseBackend(backend);
    ENG_INFO("[G.ONI] Android runtime started (backend requested: {})",
             backendName(runtime->requested_));
    return runtime;
}

AndroidRuntime::~AndroidRuntime() {
    destroyRendererAndWindow();
    ENG_INFO("[G.ONI] Runtime shutdown");
}

void AndroidRuntime::destroyRendererAndWindow() noexcept {
    // ORDEM CRÍTICA (ADR-040/missão §V): renderer+demo primeiro (backend
    // destrói TODOS os recursos), janela por último — o renderer nunca
    // toca um ANativeWindow morto e nunca há double release.
    if (demo_.renderer.has_value()) {
        if (demo_.pipeline.isValid()) {
            (void)demo_.renderer->destroyGraphicsPipeline(demo_.pipeline);
        }
        if (demo_.shader.isValid()) {
            (void)demo_.renderer->destroyShader(demo_.shader);
        }
        if (demo_.vertexBuffer.isValid()) {
            (void)demo_.renderer->destroyBuffer(demo_.vertexBuffer);
        }
        demo_ = Demo{};  // dtor do optional destrói o Renderer (backend — ADR-035)
    }
    if (window_ != nullptr) {
        releaseWindow(window_);
        window_ = nullptr;
    }
    windowKind_ = eng::rhi::NativeWindowKind::None;
    state_ = SurfaceState::NoSurface;
}

// =============================================================================
// ANativeWindow ownership (ADR-040; no-op no Linux de testes)
// =============================================================================

void AndroidRuntime::acquireWindow(void* window) noexcept {
#ifdef __ANDROID__
    if (window != nullptr) {
        ANativeWindow_acquire(static_cast<ANativeWindow*>(window));
    }
#else
    (void)window;  // testes Linux: marker opaco — sem NDK
#endif
}

void AndroidRuntime::releaseWindow(void* window) noexcept {
#ifdef __ANDROID__
    if (window != nullptr) {
        ANativeWindow_release(static_cast<ANativeWindow*>(window));
    }
#else
    (void)window;
#endif
}

// =============================================================================
// Surface / renderer (missão §VIII: renderer só existe COM surface)
// =============================================================================

bool AndroidRuntime::createRendererForWindow(std::uint32_t width, std::uint32_t height) {
    eng::rhi::RendererConfig config{};
    config.backend = requested_;
    config.enableValidation = true;
    config.applicationName = "goni.triangle";
    config.framesInFlight = 2;
    config.surface.window = eng::rhi::NativeWindowHandle{window_, windowKind_};
    config.surface.width = width;
    config.surface.height = height;

    auto created = eng::rhi::Renderer::create(config);
    if (!created) {
        // Erro preciso do backend (seleção/validação — ADR-036): logado,
        // runtime permanece NO_SURFACE (recriável na próxima surface).
        ENG_ERROR("[G.ONI] Renderer create falhou: {}", created.error().message);
        return false;
    }
    demo_.renderer.emplace(std::move(created).value());

    // Recursos do demo (missão §XVII): shader conforme o backend REAL
    // selecionado (paridade §40 — cada backend consome a sua forma).
    const bool isVulkan = demo_.renderer->activeBackend() == BackendType::Vulkan;
    eng::rhi::ShaderDesc shaderDesc{};
    shaderDesc.debugName = "triangle";
    if (isVulkan) {
        shaderDesc.vertexSpirv = kTriangleVertexSpirvBytes();
        shaderDesc.fragmentSpirv = kTriangleFragmentSpirvBytes();
    } else {
        shaderDesc.vertexGlsl = kTriangleVertexGlsl;
        shaderDesc.fragmentGlsl = kTriangleFragmentGlsl;
    }
    auto shader = demo_.renderer->createShader(shaderDesc);
    if (!shader) {
        ENG_ERROR("[G.ONI] shader do demo falhou: {}", shader.error().message);
        destroyRendererAndWindow();
        return false;
    }
    demo_.shader = shader.value();

    eng::rhi::BufferDesc bufferDesc{};
    bufferDesc.size = sizeof(TriangleVertices::kData);
    bufferDesc.usage = eng::rhi::BufferUsage::Vertex;
    bufferDesc.initialData = std::as_bytes(std::span{TriangleVertices::kData});
    auto buffer = demo_.renderer->createBuffer(bufferDesc);
    if (!buffer) {
        ENG_ERROR("[G.ONI] vertex buffer do demo falhou: {}", buffer.error().message);
        destroyRendererAndWindow();
        return false;
    }
    demo_.vertexBuffer = buffer.value();

    eng::rhi::GraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.shader = demo_.shader;
    pipelineDesc.vertexLayout.bindings.push_back({0, TriangleVertices::kStride});
    pipelineDesc.vertexLayout.attributes.push_back(
        {0, 0, 0, eng::rhi::Format::R32G32B32A32Sfloat});
    pipelineDesc.vertexLayout.attributes.push_back(
        {1, 0, 16, eng::rhi::Format::R32G32B32A32Sfloat});
    auto pipeline = demo_.renderer->createGraphicsPipeline(pipelineDesc);
    if (!pipeline) {
        ENG_ERROR("[G.ONI] pipeline do demo falhou: {}", pipeline.error().message);
        destroyRendererAndWindow();
        return false;
    }
    demo_.pipeline = pipeline.value();

    logSelection();
    return true;
}

void AndroidRuntime::logSelection() const noexcept {
    const auto& caps = demo_.renderer->capabilities();
    ENG_INFO("[G.ONI] Backend selected: {}", caps.backendName);
    ENG_INFO("[G.ONI] API version: {}", caps.apiVersion);
    ENG_INFO("[G.ONI] GPU vendor: {}", caps.device.vendor);
    ENG_INFO("[G.ONI] GPU renderer: {}", caps.device.name);
    ENG_INFO("[G.ONI] Driver: {}", caps.device.driver);
    ENG_INFO("[G.ONI] Surface status: {}",
             caps.presentation ? "presentable" : "device-only");
}

void AndroidRuntime::surfaceCreated(void* window, eng::rhi::NativeWindowKind kind,
                                     std::uint32_t width, std::uint32_t height) {
    // Robustez (missão §VI): surface nova com runtime já com surface →
    // libera a antiga PRIMEIRO (recreation) e segue.
    if (state_ == SurfaceState::Available || state_ == SurfaceState::ChangedPending) {
        ENG_WARN("[G.ONI] Surface created com surface anterior viva — recriando");
        destroyRendererAndWindow();
    }
    if (window == nullptr || kind == eng::rhi::NativeWindowKind::None) {
        ENG_WARN("[G.ONI] Surface created com janela nula — ignorado");
        state_ = SurfaceState::NoSurface;
        return;
    }
    window_ = window;
    windowKind_ = kind;
    acquireWindow(window_);  // ownership do runtime até surfaceDestroyed
    ++stats_.surfaceCreations;
    state_ = SurfaceState::Available;
    ENG_INFO("[G.ONI] Surface created ({}x{})", width, height);

    if (width > 0 && height > 0) {
        if (!createRendererForWindow(width, height)) {
            // Renderer não pôde nascer: janela liberada, estado volta a
            // NO_SURFACE — a próxima surfaceCreated tenta de novo (§XXVIII).
            destroyRendererAndWindow();
        }
    }
    // width/height == 0 (Android: tamanho ainda desconhecido em
    // surfaceCreated): renderer é criado no primeiro surfaceChanged, que
    // SEMPRE precede qualquer render (missão §VI/VIII).
}

void AndroidRuntime::surfaceChanged(std::uint32_t width, std::uint32_t height) {
    // Renderer adiado (surfaceCreated sem tamanho — Android): cria AGORA
    // com o tamanho real do primeiro surfaceChanged.
    setViewportSize(static_cast<float>(width), static_cast<float>(height));
    if (window_ != nullptr && !demo_.renderer.has_value()) {
        ENG_INFO("[G.ONI] Surface changed ({}x{}) — criando renderer", width, height);
        if (!createRendererForWindow(width, height)) {
            destroyRendererAndWindow();
        }
        return;
    }
    if (state_ == SurfaceState::Available || state_ == SurfaceState::ChangedPending) {
        pendingWidth_ = width;
        pendingHeight_ = height;
        state_ = SurfaceState::ChangedPending;
        ENG_INFO("[G.ONI] Surface changed ({}x{} pendente)", width, height);
    } else {
        // surfaceChanged sem surface viva (ordem inusual — §VI): no-op.
        ENG_INFO("[G.ONI] Surface changed sem surface viva — ignorado");
    }
}

void AndroidRuntime::surfaceDestroyed() {
    if (state_ == SurfaceState::NoSurface || state_ == SurfaceState::Destroyed) {
        // surfaceDestroyed duplicado/tardio (§VI): no-op defensivo.
        ENG_INFO("[G.ONI] Surface destroyed sem surface viva — ignorado");
        return;
    }
    destroyRendererAndWindow();  // renderer primeiro, janela depois (ADR-040)
    state_ = SurfaceState::Destroyed;
    ++stats_.surfaceDestructions;
    ENG_INFO("[G.ONI] Surface destroyed");
}

// =============================================================================
// Lifecycle (missão §VI/§XXIX)
// =============================================================================

void AndroidRuntime::setViewportSize(float width, float height) {
    input_.setScreenSize(width, height);
}

void AndroidRuntime::onTouchEvent(int canonicalPhase, std::uint32_t pointerId,
                                  float x, float y, float pressure) {
    eng::input::InputEvent event;
    event.device = eng::input::DeviceKind::Touch;
    event.pointerId = pointerId;
    switch (canonicalPhase) {
    case 0: event.touchPhase = eng::input::TouchPhase::Down; break;
    case 1: event.touchPhase = eng::input::TouchPhase::Move; break;
    case 2: event.touchPhase = eng::input::TouchPhase::Up; break;
    default: event.touchPhase = eng::input::TouchPhase::Cancelled; break;
    }
    event.x = x;
    event.y = y;
    event.pressure = pressure;
    input_.queueEvent(event);
}

void AndroidRuntime::onPause() {
    paused_ = true;
    ENG_INFO("[G.ONI] Runtime paused");
}

void AndroidRuntime::onResume() {
    paused_ = false;
    ENG_INFO("[G.ONI] Runtime resumed");
}

void AndroidRuntime::setBackend(const char* backend) {
    const BackendType parsed = parseBackend(backend);
    if (parsed == BackendType::Auto && requested_ == BackendType::Auto &&
        std::string_view{backend ? backend : "auto"} != "auto") {
        // string não reconhecida: mantém auto, mas registra honestamente.
        ENG_WARN("[G.ONI] Backend não reconhecido '{}' — usando Auto", backend);
    }
    requested_ = parsed;
    ENG_INFO("[G.ONI] Backend requested: {}", backendName(requested_));
    if (demo_.renderer.has_value()) {
        // Recria AGORA com o novo backend (a surface atual permanece).
        void* window = window_;
        const auto kind = windowKind_;
        window_ = nullptr;  // destroyRendererAndWindow não deve liberar
        destroyRendererAndWindow();
        window_ = window;
        windowKind_ = kind;
        acquireWindow(window_);
        if (createRendererForWindow(pendingWidth_ ? pendingWidth_ : 64,
                                     pendingHeight_ ? pendingHeight_ : 48)) {
            state_ = SurfaceState::Available;  // surface nunca saiu de cena
        } else {
            destroyRendererAndWindow();
        }
    }
}

// =============================================================================
// Frame (missão §VIII/§XVI)
// =============================================================================

bool AndroidRuntime::renderFrame() {

    if (paused_) {
        ++stats_.framesSkippedPaused;
        return false;  // sem trabalho gráfico em pause (§VIII)
    }
    if ((state_ != SurfaceState::Available && state_ != SurfaceState::ChangedPending) ||
        !demo_.renderer.has_value()) {
        ++stats_.framesSkippedNoSurface;
        return false;  // NO_SURFACE/DESTROYED/sem renderer: nunca renderiza (§VIII)
    }

    // Um passo de input por frame (FASE 9 §6.1) — apenas quando o runtime
    // está ativo (pausa não consome janelas de input).
    input_.update();

    // Resize pendente: backend recria swapchain/pbuffer (missão §XXVIII).
    if (state_ == SurfaceState::ChangedPending) {
        if (auto resized = demo_.renderer->resize(pendingWidth_, pendingHeight_);
            !resized) {
            ENG_WARN("[G.ONI] Surface recreation failed: {}", resized.error().message);
            return false;
        }
        state_ = SurfaceState::Available;
    }

    auto acquired = demo_.renderer->beginFrame();
    if (!acquired) {
        ENG_WARN("[G.ONI] beginFrame falhou: {}", acquired.error().message);
        return false;
    }
    const auto status = acquired.value().status;
    if (status != FrameAcquireStatus::Renderable) {
        return false;  // Minimized/OutOfDate: protocolo, não erro (§VIII)
    }
    auto& frame = acquired.value().frame;

    eng::rhi::ClearDesc clear{};
    clear.color = {0.10f, 0.10f, 0.15f, 1.f};
    (void)frame.clear(clear);
    (void)frame.setPipeline(demo_.pipeline);
    (void)frame.bindVertexBuffer(demo_.vertexBuffer);
    (void)frame.draw(TriangleVertices::kCount);
    (void)frame.end();
    ++stats_.framesSubmitted;
    if (!stats_.firstFrameSubmitted) {
        stats_.firstFrameSubmitted = true;
        ENG_INFO("[G.ONI] First frame submitted");
    }

    if (auto presented = demo_.renderer->present(); !presented) {
        ENG_WARN("[G.ONI] present falhou: {}", presented.error().message);
        return false;
    }
    ++stats_.framesPresented;
    if (!stats_.firstFramePresented) {
        stats_.firstFramePresented = true;
        ENG_INFO("[G.ONI] First frame presented");
    }
    return true;
}

// =============================================================================
// Introspecção
// =============================================================================

eng::rhi::BackendType AndroidRuntime::selectedBackend() const noexcept {
    return demo_.renderer.has_value() ? demo_.renderer->activeBackend() : requested_;
}

const eng::rhi::RendererCapabilities* AndroidRuntime::capabilities() const noexcept {
    return demo_.renderer.has_value() ? &demo_.renderer->capabilities() : nullptr;
}

}  // namespace eng::android
