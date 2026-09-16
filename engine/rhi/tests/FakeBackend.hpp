#pragma once

/// FakeBackend — TESTES APENAS (missão §11).
///
/// Backend determinístico para os testes unitários da FASE 4: exercita
/// lifecycle, handles, erros, estado, capabilities, ownership e o protocolo
/// de frame da abstraction. NUNCA é reportado como Vulkan/OpenGL ES, NUNCA
/// é backend de produção, NUNCA satisfaz testes de integração gráfica real —
/// capabilities carregam backendName "Fake (TESTES)" e softwareRendering
/// verdadeiro por construção.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "eng/rhi/RhiBackend.hpp"

namespace eng::rhi::testing {

class FakeBackend final : public RhiBackend {
public:
    // ---------------------------------------------------------------- factory

    /// Fábrica para `Renderer::registerBackend` nos testes.
    static std::unique_ptr<RhiBackend> create();

    // ---------------------------------------------------------------- tooling
    // Estado estático de TESTE — single-threaded por convenção (os testes
    // são sequenciais). `resetTestState()` é chamado no início de cada caso.

    /// Cenário consumido pela PRÓXIMA instância criada pela fábrica (permite
    /// configurar falha/probe ANTES de `Renderer::create` construir o
    /// backend). A fila é consumida em ordem de criação.
    struct Scenario {
        BackendProbe probe{Availability::Available, "fake disponível"};
        bool failInitialize{false};
        std::string initializeMessage{"falha injetada (teste)"};
        bool validationAvailable{true};
    };

    static void queueScenario(Scenario scenario);
    /// Limpa fila de cenários e rastreio de instâncias. Testes chamam no
    /// início de cada caso (pressupõe renderers anteriores destruídos).
    static void resetTestState();
    [[nodiscard]] static FakeBackend* lastCreated();
    [[nodiscard]] static std::vector<FakeBackend*> aliveInstances();

    /// Estado capturado no destrutor — para assertar liberação total.
    struct TearDown {
        std::size_t liveResources{0};
        bool hadActiveFrame{false};
        bool wasInitialized{false};
    };
    [[nodiscard]] static TearDown lastTearDown();

    // ------------------------------------------------------- injeção de cenário

    /// Próximo beginFrame devolve este status (one-shot; OutOfDate/Minimized
    /// são protocolo, missão §12).
    void setNextAcquire(FrameAcquireStatus status);
    void setSurfaceLost(bool lost);

    // ------------------------------------------------------------- inspeção

    /// Log de operações em ordem ("init", "begin", "clear", "pipeline",
    /// "vbo", "draw:3", "end", "present", "resize:640x480", ...).
    [[nodiscard]] const std::vector<std::string>& ops() const noexcept { return ops_; }
    [[nodiscard]] std::size_t liveResources() const noexcept;
    [[nodiscard]] bool hasActiveFrame() const noexcept { return phase_ == Phase::Recording; }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] bool hasSurface() const noexcept { return hasSurface_; }
    [[nodiscard]] std::uint32_t recreateCount() const noexcept { return recreateCount_; }
    [[nodiscard]] std::uint32_t presentCount() const noexcept { return presentCount_; }
    [[nodiscard]] std::uint32_t resizeCount() const noexcept { return resizeCount_; }
    [[nodiscard]] std::size_t bufferCount() const noexcept { return buffers_.size(); }
    /// Conteúdo atual do buffer (para validar update/cópia inicial).
    [[nodiscard]] std::vector<std::byte> bufferData(BufferHandle handle) const;

    // ---------------------------------------------------------- RhiBackend impl

    [[nodiscard]] BackendProbe probe() override;
    eng::core::Result<void> initialize(const RendererConfig& config,
                                       RendererCapabilities& outCapabilities) override;
    [[nodiscard]] const RendererCapabilities& capabilities() const override;

    [[nodiscard]] eng::core::Result<BufferHandle> createBuffer(const BufferDesc& desc) override;
    [[nodiscard]] eng::core::Result<ShaderHandle> createShader(const ShaderDesc& desc) override;
    [[nodiscard]] eng::core::Result<GraphicsPipelineHandle> createGraphicsPipeline(
        const GraphicsPipelineDesc& desc) override;
    eng::core::Result<void> updateBuffer(BufferHandle handle, std::size_t offset,
                                         std::span<const std::byte> data) override;
    eng::core::Result<void> destroyBuffer(BufferHandle handle) override;
    eng::core::Result<void> destroyShader(ShaderHandle handle) override;
    eng::core::Result<void> destroyGraphicsPipeline(GraphicsPipelineHandle handle) override;

    [[nodiscard]] eng::core::Result<BeginFrameResult> beginFrame() override;
    eng::core::Result<void> frameClear(std::uint64_t frameId, const ClearDesc& clear) override;
    eng::core::Result<void> frameSetViewport(std::uint64_t frameId,
                                             const Viewport& viewport) override;
    eng::core::Result<void> frameSetPipeline(std::uint64_t frameId,
                                              GraphicsPipelineHandle pipeline) override;
    eng::core::Result<void> frameBindVertexBuffer(std::uint64_t frameId,
                                                   BufferHandle buffer) override;
    eng::core::Result<void> frameBindIndexBuffer(std::uint64_t frameId, BufferHandle buffer,
                                                 IndexType indexType) override;
    eng::core::Result<void> frameDraw(std::uint64_t frameId, std::uint32_t vertexCount,
                                      std::uint32_t firstVertex) override;
    eng::core::Result<void> frameDrawIndexed(std::uint64_t frameId, std::uint32_t indexCount,
                                              std::uint32_t firstIndex) override;
    eng::core::Result<void> endFrame(std::uint64_t frameId) override;
    eng::core::Result<void> present() override;
    eng::core::Result<void> resize(std::uint32_t width, std::uint32_t height) override;
    [[nodiscard]] bool surfaceLost() const override;

private:
    FakeBackend();
    ~FakeBackend() override;

    struct BufferEntry {
        std::uint32_t generation{1};
        std::size_t size{0};
        BufferUsage usage{BufferUsage::None};
        std::vector<std::byte> data{};
    };
    struct ShaderEntry {
        std::uint32_t generation{1};
    };
    struct PipelineEntry {
        std::uint32_t generation{1};
        ShaderHandle shader{};
    };

    // Handles: índice em 32 bits baixos (+1), geração em 32 bits altos.
    [[nodiscard]] static std::uint64_t encodeHandle(std::uint32_t index,
                                                    std::uint32_t generation) noexcept {
        return (static_cast<std::uint64_t>(generation) << 32) |
               (static_cast<std::uint64_t>(index) + 1u);
    }

    [[nodiscard]] eng::core::Result<void> requireInitialized() const;
    [[nodiscard]] eng::core::Result<void> requireRecording(std::uint64_t frameId) const;
    void record(std::string op);

    Scenario scenario_{};
    bool initialized_{false};
    bool hasSurface_{false};
    bool surfaceLost_{false};
    std::optional<FrameAcquireStatus> nextAcquire_{};

    std::map<std::uint32_t, BufferEntry> buffers_{};
    std::map<std::uint32_t, ShaderEntry> shaders_{};
    std::map<std::uint32_t, PipelineEntry> pipelines_{};
    std::map<std::uint32_t, std::uint32_t> freeBufferSlots_{};
    std::map<std::uint32_t, std::uint32_t> freeShaderSlots_{};
    std::map<std::uint32_t, std::uint32_t> freePipelineSlots_{};
    std::uint32_t nextBufferIndex_{0};
    std::uint32_t nextShaderIndex_{0};
    std::uint32_t nextPipelineIndex_{0};

    enum class Phase { Idle, Recording, Ended };
    Phase phase_{Phase::Idle};
    std::uint64_t activeFrameId_{0};
    /// Frames submetidos aguardando present (auditoria FASE 5, L3).
    std::uint32_t pendingPresents_{0};
    bool pipelineSet_{false};
    bool vertexBound_{false};
    bool indexBound_{false};

    RendererCapabilities capabilities_{};
    std::vector<std::string> ops_{};
    std::uint32_t recreateCount_{0};
    std::uint32_t presentCount_{0};
    std::uint32_t resizeCount_{0};
};

} // namespace eng::rhi::testing
