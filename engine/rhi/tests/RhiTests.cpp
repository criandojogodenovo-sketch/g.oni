#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "eng/rhi/Renderer.hpp"

#include "FakeBackend.hpp"

// =============================================================================
// Helpers
// =============================================================================

namespace {

using eng::rhi::BackendType;
using eng::rhi::BufferHandle;
using eng::rhi::BufferUsage;
using eng::rhi::FrameAcquireStatus;
using eng::rhi::GraphicsPipelineDesc;
using eng::rhi::GraphicsPipelineHandle;
using eng::rhi::NativeWindowHandle;
using eng::rhi::NativeWindowKind;
using eng::rhi::Renderer;
using eng::rhi::RendererConfig;
using eng::rhi::ShaderDesc;
using eng::rhi::ShaderHandle;
using eng::rhi::ValidationState;
using eng::rhi::testing::FakeBackend;

/// Estado de teste limpo: registry vazio + rastreio do fake zerado.
/// (clearRegisteredBackends/resetTestState são API "somente testes".)
void resetRhi() {
    Renderer::clearRegisteredBackends();
    FakeBackend::resetTestState();
}

void registerFakeBackends() {
    REQUIRE(Renderer::registerBackend(BackendType::Vulkan, &FakeBackend::create).ok());
    REQUIRE(Renderer::registerBackend(BackendType::OpenGLES, &FakeBackend::create).ok());
}

/// Config com surface "headless" válida.
RendererConfig surfaceConfig() {
    RendererConfig config{};
    config.surface.window =
        NativeWindowHandle{reinterpret_cast<const void*>(0x1234), NativeWindowKind::Headless};
    config.surface.width = 64;
    config.surface.height = 48;
    return config;
}

eng::rhi::BufferDesc vertexBufferDesc(std::size_t size = 256) {
    eng::rhi::BufferDesc desc{};
    desc.size = size;
    desc.usage = BufferUsage::Vertex;
    return desc;
}

/// Cria renderer Vulkan (fake) e devolve {renderer, fake}.
struct RendererAndFake {
    Renderer renderer;
    FakeBackend* fake{nullptr};
};

RendererAndFake makeRenderer(const RendererConfig& config) {
    auto created = Renderer::create(config);
    REQUIRE(created.ok());
    RendererAndFake out{std::move(created).value(), FakeBackend::lastCreated()};
    REQUIRE(out.fake != nullptr);
    return out;
}

const std::byte* payload8() {
    static const std::byte bytes[8]{};
    return bytes;
}

} // namespace

// =============================================================================
// Registro e seleção (missão §10)
// =============================================================================

TEST_CASE("rhi: registro de backends e create sem nenhum registro", "[rhi]")
{
    resetRhi();

    SECTION("create sem nenhum backend registrado") {
        const auto created = Renderer::create(RendererConfig{});
        REQUIRE(created.isError());
        CHECK(created.error().code == eng::core::StatusCode::NotFound);
        CHECK(created.error().message.find("nenhum backend registrado") !=
              std::string::npos);
    }

    SECTION("registro duplicado é rejeitado") {
        REQUIRE(Renderer::registerBackend(BackendType::Vulkan, &FakeBackend::create).ok());
        const auto again =
            Renderer::registerBackend(BackendType::Vulkan, &FakeBackend::create);
        REQUIRE(again.isError());
        CHECK(again.error().code == eng::core::StatusCode::AlreadyExists);
    }

    SECTION("registeredBackends em ordem de preferência") {
        REQUIRE(Renderer::registerBackend(BackendType::OpenGLES, &FakeBackend::create).ok());
        REQUIRE(Renderer::registerBackend(BackendType::Vulkan, &FakeBackend::create).ok());
        const auto registered = Renderer::registeredBackends();
        REQUIRE(registered.size() == 2);
        CHECK(registered[0] == BackendType::Vulkan);
        CHECK(registered[1] == BackendType::OpenGLES);
    }
}

TEST_CASE("rhi: Auto seleciona por ordem de preferência com motivos", "[rhi]")
{
    resetRhi();

    SECTION("Auto escolhe Vulkan quando disponível") {
        registerFakeBackends();
        auto renderer = makeRenderer(RendererConfig{});
        CHECK(renderer.renderer.activeBackend() == BackendType::Vulkan);
    }

    SECTION("Auto cai para OpenGL ES quando Vulkan indisponível no probe") {
        FakeBackend::Scenario vulkanUnavailable{};
        vulkanUnavailable.probe = {eng::rhi::Availability::Unavailable,
                                   "loader ausente (injetado)"};
        FakeBackend::queueScenario(vulkanUnavailable);
        registerFakeBackends();
        auto renderer = makeRenderer(RendererConfig{});
        CHECK(renderer.renderer.activeBackend() == BackendType::OpenGLES);
    }

    SECTION("Auto cai para OpenGL ES quando a inicialização Vulkan falha") {
        FakeBackend::Scenario vulkanFails{};
        vulkanFails.failInitialize = true;
        FakeBackend::queueScenario(vulkanFails);
        registerFakeBackends();
        auto renderer = makeRenderer(RendererConfig{});
        CHECK(renderer.renderer.activeBackend() == BackendType::OpenGLES);
    }

    SECTION("Auto com apenas GLES registrado escolhe GLES") {
        REQUIRE(Renderer::registerBackend(BackendType::OpenGLES, &FakeBackend::create).ok());
        auto renderer = makeRenderer(RendererConfig{});
        CHECK(renderer.renderer.activeBackend() == BackendType::OpenGLES);
    }

    SECTION("Auto sem nenhum disponível agrega TODOS os motivos") {
        FakeBackend::Scenario vulkanUnavailable{};
        vulkanUnavailable.probe = {eng::rhi::Availability::Unavailable, "sem ICD"};
        FakeBackend::Scenario glesUnavailable{};
        glesUnavailable.probe = {eng::rhi::Availability::Unavailable, "sem libEGL"};
        FakeBackend::queueScenario(vulkanUnavailable);
        FakeBackend::queueScenario(glesUnavailable);
        registerFakeBackends();
        const auto created = Renderer::create(RendererConfig{});
        REQUIRE(created.isError());
        CHECK(created.error().code == eng::core::StatusCode::NotSupported);
        CHECK(created.error().message.find("Vulkan") != std::string::npos);
        CHECK(created.error().message.find("sem ICD") != std::string::npos);
        CHECK(created.error().message.find("OpenGL ES") != std::string::npos);
        CHECK(created.error().message.find("sem libEGL") != std::string::npos);
    }
}

TEST_CASE("rhi: seleção explícita não faz fallback silencioso", "[rhi]")
{
    resetRhi();

    SECTION("explícito não registrado") {
        REQUIRE(Renderer::registerBackend(BackendType::Vulkan, &FakeBackend::create).ok());
        RendererConfig config = surfaceConfig();
        config.backend = BackendType::OpenGLES;
        const auto created = Renderer::create(config);
        REQUIRE(created.isError());
        CHECK(created.error().code == eng::core::StatusCode::NotFound);
        CHECK(created.error().message.find("não registrado") != std::string::npos);
    }

    SECTION("explícito com falha de inicialização: erro preciso, sem fallback") {
        FakeBackend::Scenario vulkanFails{};
        vulkanFails.failInitialize = true;
        vulkanFails.initializeMessage = "fila de graphics ausente (injetado)";
        FakeBackend::queueScenario(vulkanFails);
        registerFakeBackends();
        RendererConfig config = surfaceConfig();
        config.backend = BackendType::Vulkan;
        const auto created = Renderer::create(config);
        REQUIRE(created.isError());
        CHECK(created.error().message.find("fila de graphics ausente") != std::string::npos);
        // A instância que falhou foi destruída na saída — sem leak.
        CHECK(FakeBackend::lastCreated() == nullptr);
    }

    SECTION("allowFallback explícito prossegue para o próximo backend") {
        FakeBackend::Scenario vulkanFails{};
        vulkanFails.failInitialize = true;
        FakeBackend::queueScenario(vulkanFails);
        registerFakeBackends();
        RendererConfig config = surfaceConfig();
        config.backend = BackendType::Vulkan;
        config.allowFallback = true;
        auto renderer = makeRenderer(config);
        CHECK(renderer.renderer.activeBackend() == BackendType::OpenGLES);
    }
}

// =============================================================================
// Validação e capabilities (missão §9/§18)
// =============================================================================

TEST_CASE("rhi: estado de validação honesto (missão §18)", "[rhi]")
{
    resetRhi();

    SECTION("pedida e disponível → Enabled") {
        registerFakeBackends();
        RendererConfig config = surfaceConfig();
        config.enableValidation = true;
        auto renderer = makeRenderer(config);
        CHECK(renderer.renderer.validationState() == ValidationState::Enabled);
    }

    SECTION("pedida e ausente → Unavailable (nunca finge Enabled)") {
        FakeBackend::Scenario noValidation{};
        noValidation.validationAvailable = false;
        FakeBackend::queueScenario(noValidation);
        registerFakeBackends();
        RendererConfig config = surfaceConfig();
        config.enableValidation = true;
        auto renderer = makeRenderer(config);
        CHECK(renderer.renderer.validationState() == ValidationState::Unavailable);
    }

    SECTION("não pedida → DisabledByConfiguration") {
        registerFakeBackends();
        RendererConfig config = surfaceConfig();
        config.enableValidation = false;
        auto renderer = makeRenderer(config);
        CHECK(renderer.renderer.validationState() ==
              ValidationState::DisabledByConfiguration);
    }
}

TEST_CASE("rhi: capabilities refletem o backend real em uso", "[rhi]")
{
    resetRhi();
    registerFakeBackends();

    SECTION("com surface: presentation true; fake marcado como fake") {
        auto renderer = makeRenderer(surfaceConfig());
        const auto& caps = renderer.renderer.capabilities();
        CHECK(caps.backendName == "Fake (TESTES)");
        CHECK(caps.softwareRendering);  // honestidade — nunca hardware
        CHECK(caps.presentation);
        CHECK(caps.device.kind == eng::rhi::DeviceKind::Cpu);
        CHECK(caps.maxTextureSize == 1024);
        CHECK(caps.supportedFormats.size() == 3);
        CHECK(renderer.renderer.hasSurface());
    }

    SECTION("device-only: presentation false (missão §13)") {
        auto renderer = makeRenderer(RendererConfig{});
        CHECK_FALSE(renderer.renderer.hasSurface());
        CHECK_FALSE(renderer.renderer.capabilities().presentation);
    }
}

// =============================================================================
// Recursos: handles, ownership, stale (missão §8)
// =============================================================================

TEST_CASE("rhi: buffers — criação, dados, update, destroy", "[rhi]")
{
    resetRhi();
    registerFakeBackends();
    auto renderer = makeRenderer(RendererConfig{});

    const std::byte payload[4]{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    eng::rhi::BufferDesc desc{};
    desc.size = 8;
    desc.usage = BufferUsage::Vertex;
    desc.initialData = payload;

    auto created = renderer.renderer.createBuffer(desc);
    REQUIRE(created.ok());
    const BufferHandle handle = created.value();
    CHECK(handle.isValid());

    SECTION("dados iniciais persistem e update funciona") {
        const auto data = renderer.fake->bufferData(handle);
        REQUIRE(data.size() == 8);
        CHECK(std::memcmp(data.data(), payload, 4) == 0);

        const std::byte patch[2]{std::byte{9}, std::byte{9}};
        REQUIRE(renderer.renderer.updateBuffer(handle, 4, patch).ok());
        const auto updated = renderer.fake->bufferData(handle);
        CHECK(updated[4] == std::byte{9});
        CHECK(updated[5] == std::byte{9});
        CHECK(updated[6] == std::byte{0});
    }

    SECTION("update fora dos limites é rejeitado com erro preciso") {
        const std::byte patch[4]{};
        auto updated = renderer.renderer.updateBuffer(handle, 6, patch);
        REQUIRE(updated.isError());
        CHECK(updated.error().code == eng::core::StatusCode::InvalidArgument);
    }

    SECTION("destroy e double-destroy") {
        REQUIRE(renderer.renderer.destroyBuffer(handle).ok());
        auto destroyed = renderer.renderer.destroyBuffer(handle);
        REQUIRE(destroyed.isError());
        CHECK(destroyed.error().code == eng::core::StatusCode::InvalidArgument);
    }

    SECTION("stale handle é detectado via geração") {
        REQUIRE(renderer.renderer.destroyBuffer(handle).ok());
        // Recriação reusa o slot com geração nova — o handle antigo é stale.
        auto recreated = renderer.renderer.createBuffer(vertexBufferDesc());
        REQUIRE(recreated.ok());
        CHECK(recreated.value().id != handle.id);
        auto updated = renderer.renderer.updateBuffer(handle, 0, {});
        REQUIRE(updated.isError());
        CHECK(updated.error().message.find("stale") != std::string::npos);
    }

    SECTION("handles nulos rejeitados no frontend") {
        auto updated = renderer.renderer.updateBuffer(BufferHandle{}, 0, {});
        REQUIRE(updated.isError());
        CHECK(updated.error().message.find("nulo") != std::string::npos);
        auto destroyed = renderer.renderer.destroyBuffer(BufferHandle{});
        REQUIRE(destroyed.isError());
    }
}

TEST_CASE("rhi: validação de descritores no frontend", "[rhi]")
{
    resetRhi();
    registerFakeBackends();
    auto renderer = makeRenderer(RendererConfig{});

    SECTION("buffer: tamanho zero / uso None / initialData maior") {
        auto bad = renderer.renderer.createBuffer(vertexBufferDesc(0));
        REQUIRE(bad.isError());
        CHECK(bad.error().message.find("tamanho zero") != std::string::npos);

        eng::rhi::BufferDesc noUsage{};
        noUsage.size = 16;
        bad = renderer.renderer.createBuffer(noUsage);
        REQUIRE(bad.isError());
        CHECK(bad.error().message.find("uso None") != std::string::npos);

        eng::rhi::BufferDesc tooBig{};
        tooBig.size = 4;
        tooBig.usage = BufferUsage::Vertex;
        tooBig.initialData = std::span<const std::byte>{payload8(), 8};
        bad = renderer.renderer.createBuffer(tooBig);
        REQUIRE(bad.isError());
        CHECK(bad.error().message.find("maior que size") != std::string::npos);
    }

    SECTION("shader: sem nenhuma representação") {
        auto bad = renderer.renderer.createShader(ShaderDesc{});
        REQUIRE(bad.isError());
        CHECK(bad.error().message.find("nenhuma representação") != std::string::npos);
    }

    SECTION("pipeline: sem shader / layout vazio / binding não declarado / formato") {
        GraphicsPipelineDesc desc{};
        eng::rhi::VertexLayout layout{};
        layout.bindings.push_back({0, 24});
        layout.attributes.push_back({0, 0, 0, eng::rhi::Format::R8G8B8A8Srgb});
        desc.vertexLayout = layout;

        auto bad = renderer.renderer.createGraphicsPipeline(desc);
        REQUIRE(bad.isError());
        CHECK(bad.error().message.find("sem shader") != std::string::npos);

        auto shader = renderer.renderer.createShader(ShaderDesc{});
        REQUIRE(shader.isError());  // ainda sem representação

        ShaderDesc shaderDesc{};
        shaderDesc.vertexGlsl = "void main(){}";
        shaderDesc.fragmentGlsl = "void main(){}";
        auto createdShader = renderer.renderer.createShader(shaderDesc);
        REQUIRE(createdShader.ok());
        desc.shader = createdShader.value();

        // Layout vazio rejeitado (bindings/attributes ausentes)
        desc.vertexLayout = eng::rhi::VertexLayout{};
        auto emptyLayout = renderer.renderer.createGraphicsPipeline(desc);
        REQUIRE(emptyLayout.isError());

        eng::rhi::VertexLayout badLayout = layout;
        badLayout.attributes.push_back({1, 7, 0, eng::rhi::Format::R32G32B32A32Sfloat});
        desc.vertexLayout = badLayout;
        auto undeclared = renderer.renderer.createGraphicsPipeline(desc);
        REQUIRE(undeclared.isError());
        CHECK(undeclared.error().message.find("binding 7") != std::string::npos);

        desc.vertexLayout = layout;
        desc.renderTarget.colorFormat = eng::rhi::Format::Undefined;
        auto badFormat = renderer.renderer.createGraphicsPipeline(desc);
        REQUIRE(badFormat.isError());
        CHECK(badFormat.error().message.find("Undefined") != std::string::npos);
    }
}

// =============================================================================
// Frame lifecycle (missão §12)
// =============================================================================

TEST_CASE("rhi: frame happy path com ordem exata de operações", "[rhi]")
{
    resetRhi();
    registerFakeBackends();
    auto renderer = makeRenderer(surfaceConfig());

    auto shader = renderer.renderer.createShader([] {
        ShaderDesc desc{};
        desc.vertexGlsl = "void main(){}";
        desc.fragmentGlsl = "void main(){}";
        return desc;
    }());
    REQUIRE(shader.ok());

    auto buffer = renderer.renderer.createBuffer(vertexBufferDesc());
    REQUIRE(buffer.ok());

    GraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.shader = shader.value();
    eng::rhi::VertexLayout layout{};
    layout.bindings.push_back({0, 24});
    layout.attributes.push_back({0, 0, 0, eng::rhi::Format::R8G8B8A8Srgb});
    pipelineDesc.vertexLayout = layout;
    auto pipeline = renderer.renderer.createGraphicsPipeline(pipelineDesc);
    REQUIRE(pipeline.ok());

    auto acquired = renderer.renderer.beginFrame();
    REQUIRE(acquired.ok());
    REQUIRE(acquired.value().status == FrameAcquireStatus::Renderable);
    auto& frame = acquired.value().frame;
    REQUIRE(frame.isValid());

    REQUIRE(frame.clear({}).ok());
    eng::rhi::Viewport viewport{};
    viewport.width = 64.f;
    viewport.height = 48.f;
    REQUIRE(frame.setViewport(viewport).ok());
    REQUIRE(frame.setPipeline(pipeline.value()).ok());
    REQUIRE(frame.bindVertexBuffer(buffer.value()).ok());
    REQUIRE(frame.draw(3).ok());
    REQUIRE(frame.end().ok());
    CHECK(frame.isEnded());
    REQUIRE(renderer.renderer.present().ok());

    // Segundo frame após present
    auto second = renderer.renderer.beginFrame();
    REQUIRE(second.ok());
    REQUIRE(second.value().frame.isValid());
    REQUIRE(second.value().frame.end().ok());
    REQUIRE(renderer.renderer.present().ok());

    const auto& ops = renderer.fake->ops();
    // init + 3 criações + frame1 (8 ops) + frame2 (3 ops)
    REQUIRE(ops.size() == 15);
    CHECK(ops[0] == "init");
    CHECK(ops[4] == "begin");
    CHECK(ops[5] == "clear");
    CHECK(ops[6] == "viewport");
    CHECK(ops[7] == "pipeline");
    CHECK(ops[8] == "vbo");
    CHECK(ops[9] == "draw:3");
    CHECK(ops[10] == "end");
    CHECK(ops[11] == "present");
    CHECK(ops[12] == "begin");
    CHECK(renderer.fake->presentCount() == 2);
}

TEST_CASE("rhi: erros de protocolo do frame", "[rhi]")
{
    resetRhi();
    registerFakeBackends();
    auto renderer = makeRenderer(surfaceConfig());

    auto shader = renderer.renderer.createShader([] {
        ShaderDesc desc{};
        desc.vertexGlsl = "v";
        desc.fragmentGlsl = "f";
        return desc;
    }());
    REQUIRE(shader.ok());
    auto buffer = renderer.renderer.createBuffer(vertexBufferDesc());
    REQUIRE(buffer.ok());
    auto indexBuffer = renderer.renderer.createBuffer([] {
        eng::rhi::BufferDesc desc{};
        desc.size = 6;
        desc.usage = BufferUsage::Index;
        return desc;
    }());
    REQUIRE(indexBuffer.ok());

    GraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.shader = shader.value();
    eng::rhi::VertexLayout layout{};
    layout.bindings.push_back({0, 12});
    layout.attributes.push_back({0, 0, 0, eng::rhi::Format::R8G8B8A8Unorm});
    pipelineDesc.vertexLayout = layout;
    auto pipeline = renderer.renderer.createGraphicsPipeline(pipelineDesc);
    REQUIRE(pipeline.ok());

    SECTION("present sem frame") {
        auto presented = renderer.renderer.present();
        REQUIRE(presented.isError());
        CHECK(presented.error().code == eng::core::StatusCode::NotSupported);
    }

    SECTION("draw sem pipeline / sem vbo; drawIndexed sem ibo") {
        auto acquired = renderer.renderer.beginFrame();
        REQUIRE(acquired.ok());
        auto& frame = acquired.value().frame;

        CHECK(frame.draw(3).isError());
        REQUIRE(frame.setPipeline(pipeline.value()).ok());
        CHECK(frame.draw(3).isError());
        REQUIRE(frame.bindVertexBuffer(buffer.value()).ok());

        auto indexed = frame.drawIndexed(3);
        REQUIRE(indexed.isError());
        CHECK(indexed.error().message.find("ibo") != std::string::npos);

        REQUIRE(frame.draw(3).ok());
        REQUIRE(frame.end().ok());
    }

    SECTION("buffer sem uso Vertex não binda como VBO") {
        auto acquired = renderer.renderer.beginFrame();
        REQUIRE(acquired.ok());
        auto& frame = acquired.value().frame;
        REQUIRE(frame.setPipeline(pipeline.value()).ok());
        auto bound = frame.bindVertexBuffer(indexBuffer.value());
        REQUIRE(bound.isError());
        CHECK(bound.error().message.find("uso Vertex") != std::string::npos);
    }

    SECTION("end duplo e operações pós-end") {
        auto acquired = renderer.renderer.beginFrame();
        REQUIRE(acquired.ok());
        auto& frame = acquired.value().frame;
        REQUIRE(frame.end().ok());
        CHECK(frame.end().isError());
        CHECK(frame.clear({}).isError());
        CHECK(frame.draw(3).isError());
        REQUIRE(renderer.renderer.present().ok());
    }

    SECTION("beginFrame com frame anterior em gravação") {
        auto first = renderer.renderer.beginFrame();
        REQUIRE(first.ok());
        auto second = renderer.renderer.beginFrame();
        REQUIRE(second.isError());
        CHECK(second.error().message.find("não finalizado") != std::string::npos);
    }

    SECTION("RAII: destruir Frame pendente executa end fail-safe") {
        {
            auto acquired = renderer.renderer.beginFrame();
            REQUIRE(acquired.ok());
            REQUIRE(acquired.value().frame.isValid());
        }  // Frame destruído sem end()
        const auto& ops = renderer.fake->ops();
        REQUIRE(ops.size() >= 2);
        CHECK(ops[ops.size() - 1] == "end");
        CHECK_FALSE(renderer.fake->hasActiveFrame());
        // Próximo frame começa limpo
        auto next = renderer.renderer.beginFrame();
        REQUIRE(next.ok());
        REQUIRE(next.value().frame.isValid());
    }

    SECTION("Frame moved-from é inutilizável de forma definida") {
        auto acquired = renderer.renderer.beginFrame();
        REQUIRE(acquired.ok());
        auto frame = std::move(acquired.value().frame);
        auto moved = std::move(frame);
        CHECK_FALSE(frame.isValid());
        CHECK(frame.draw(3).isError());
        CHECK(frame.clear({}).isError());
        CHECK(moved.isValid());
        REQUIRE(moved.setPipeline(pipeline.value()).ok());
    }
}

TEST_CASE("rhi: estados de protocolo OutOfDate/Minimized não são erro", "[rhi]")
{
    resetRhi();
    registerFakeBackends();
    auto renderer = makeRenderer(surfaceConfig());

    SECTION("Minimized: status + frame inválido; próximo begin funciona") {
        renderer.fake->setNextAcquire(FrameAcquireStatus::Minimized);
        auto acquired = renderer.renderer.beginFrame();
        REQUIRE(acquired.ok());
        CHECK(acquired.value().status == FrameAcquireStatus::Minimized);
        CHECK_FALSE(acquired.value().frame.isValid());
        // present sem frame submetido segue como erro preciso
        CHECK(renderer.renderer.present().isError());

        auto next = renderer.renderer.beginFrame();
        REQUIRE(next.ok());
        CHECK(next.value().status == FrameAcquireStatus::Renderable);
    }

    SECTION("OutOfDate: recreação contabilizada; próximo begin renderiza") {
        renderer.fake->setNextAcquire(FrameAcquireStatus::OutOfDate);
        auto acquired = renderer.renderer.beginFrame();
        REQUIRE(acquired.ok());
        CHECK(acquired.value().status == FrameAcquireStatus::OutOfDate);
        CHECK_FALSE(acquired.value().frame.isValid());
        CHECK(renderer.fake->recreateCount() == 1);
        CHECK_FALSE(renderer.fake->hasActiveFrame());

        auto next = renderer.renderer.beginFrame();
        REQUIRE(next.ok());
        CHECK(next.value().status == FrameAcquireStatus::Renderable);
    }
}

// =============================================================================
// Device-only vs presentation (missão §13) e surface/resize
// =============================================================================

TEST_CASE("rhi: device-only separa device de presentation (missão §13)", "[rhi]")
{
    resetRhi();
    registerFakeBackends();
    auto renderer = makeRenderer(RendererConfig{});  // sem surface

    // Recursos e capabilities funcionam sem surface.
    auto buffer = renderer.renderer.createBuffer(vertexBufferDesc());
    REQUIRE(buffer.ok());
    CHECK(renderer.renderer.capabilities().presentation == false);

    // Frames e resize exigem surface: erro preciso, não crash.
    auto acquired = renderer.renderer.beginFrame();
    REQUIRE(acquired.isError());
    CHECK(acquired.error().code == eng::core::StatusCode::NotSupported);
    CHECK(acquired.error().message.find("surface") != std::string::npos);

    auto resized = renderer.renderer.resize(32, 32);
    REQUIRE(resized.isError());
    CHECK(resized.error().code == eng::core::StatusCode::NotSupported);
}

TEST_CASE("rhi: surface perdida, resize e validação de dimensões", "[rhi]")
{
    resetRhi();
    registerFakeBackends();
    auto renderer = makeRenderer(surfaceConfig());

    SECTION("surfaceLost é erro recuperável; resize restaura") {
        renderer.fake->setSurfaceLost(true);
        auto acquired = renderer.renderer.beginFrame();
        REQUIRE(acquired.isError());
        CHECK(acquired.error().code == eng::core::StatusCode::NotSupported);
        CHECK(acquired.error().message.find("perdida") != std::string::npos);
        CHECK(renderer.fake->surfaceLost());

        REQUIRE(renderer.renderer.resize(32, 32).ok());
        CHECK(renderer.fake->resizeCount() == 1);
        auto next = renderer.renderer.beginFrame();
        REQUIRE(next.ok());
        CHECK(next.value().status == FrameAcquireStatus::Renderable);
    }

    SECTION("resize com dimensão zero é InvalidArgument") {
        auto resized = renderer.renderer.resize(0, 32);
        REQUIRE(resized.isError());
        CHECK(resized.error().code == eng::core::StatusCode::InvalidArgument);
    }

    SECTION("configs de surface inconsistentes são rejeitadas no create") {
        RendererConfig badWindow = surfaceConfig();
        badWindow.surface.window = NativeWindowHandle{};  // dims sem window
        const auto created = Renderer::create(badWindow);
        REQUIRE(created.isError());
        CHECK(created.error().message.find("sem window") != std::string::npos);

        RendererConfig badDims = surfaceConfig();
        badDims.surface.width = 0;  // window sem dims
        const auto created2 = Renderer::create(badDims);
        REQUIRE(created2.isError());
        CHECK(created2.error().message.find("sem dimensões") != std::string::npos);

        RendererConfig badFif = surfaceConfig();
        badFif.framesInFlight = 0;
        const auto created3 = Renderer::create(badFif);
        REQUIRE(created3.isError());
        CHECK(created3.error().message.find("framesInFlight") != std::string::npos);
    }
}

// =============================================================================
// Move semantics e ownership total (missão §8/§15)
// =============================================================================

TEST_CASE("rhi: Renderer move-only; moved-from é defensivo, não UB", "[rhi]")
{
    resetRhi();
    registerFakeBackends();

    auto created = Renderer::create(surfaceConfig());
    REQUIRE(created.ok());
    auto renderer = std::move(created).value();
    const auto* fakeBefore = FakeBackend::lastCreated();
    REQUIRE(fakeBefore != nullptr);

    auto movedTo = std::move(renderer);
    CHECK_FALSE(renderer.hasSurface());
    CHECK(renderer.activeBackend() == BackendType::Auto);
    CHECK(renderer.capabilities().backendName.empty());
    CHECK(renderer.createBuffer(vertexBufferDesc()).isError());
    CHECK(renderer.beginFrame().isError());
    CHECK(renderer.present().isError());
    CHECK(renderer.resize(8, 8).isError());

    // O renderer movido continua funcional com o MESMO backend.
    REQUIRE(movedTo.hasSurface());
    CHECK(FakeBackend::lastCreated() == fakeBefore);
    auto buffer = movedTo.createBuffer(vertexBufferDesc());
    REQUIRE(buffer.ok());
    REQUIRE(movedTo.destroyBuffer(buffer.value()).ok());
}

TEST_CASE("rhi: destruição do Renderer libera todos os recursos", "[rhi]")
{
    resetRhi();
    registerFakeBackends();

    {
        auto renderer = makeRenderer(surfaceConfig());
        REQUIRE(renderer.fake->liveResources() == 0);
        auto buffer = renderer.renderer.createBuffer(vertexBufferDesc());
        REQUIRE(buffer.ok());
        auto shader = renderer.renderer.createShader([] {
            ShaderDesc desc{};
            desc.vertexGlsl = "v";
            desc.fragmentGlsl = "f";
            return desc;
        }());
        REQUIRE(shader.ok());
        REQUIRE(renderer.renderer.destroyBuffer(buffer.value()).ok());
        REQUIRE(renderer.fake->liveResources() == 1);  // só o shader resta
    }  // Renderer destruído com 1 recurso pendente: o backend libera tudo
       // (validado por LSan no preset de debug) e reporta o pendente.

    const auto tearDown = FakeBackend::lastTearDown();
    CHECK(tearDown.liveResources == 1);  // shader não destruído explicitamente
    CHECK(tearDown.wasInitialized);
}

TEST_CASE("rhi: Frame vivo mantém o backend vivo (sem UB pós-Renderer)", "[rhi]")
{
    resetRhi();
    registerFakeBackends();

    eng::rhi::Frame lingering{};
    {
        auto renderer = makeRenderer(surfaceConfig());
        auto acquired = renderer.renderer.beginFrame();
        REQUIRE(acquired.ok());
        REQUIRE(acquired.value().status == FrameAcquireStatus::Renderable);
        lingering = std::move(acquired.value().frame);
    }  // Renderer destruído AQUI, com frame em gravação

    // O frame sobrevive com comportamento DEFINIDO: operações funcionam.
    REQUIRE(lingering.isValid());
    CHECK(lingering.clear({}).ok());

    // end() explícito após a morte do renderer também funciona.
    CHECK(lingering.end().ok());
    CHECK(lingering.isEnded());

    // Estado final consistente (backend liberado só depois do frame).
    const auto tearDown = FakeBackend::lastTearDown();
    CHECK(tearDown.liveResources == 0);
}
