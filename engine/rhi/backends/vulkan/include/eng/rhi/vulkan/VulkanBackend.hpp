#pragma once

/// eng::rhi::vulkan — backend Vulkan REAL (FASE 5, missão §17–§29).
///
/// Implementa `eng::rhi::RhiBackend` com a API Vulkan real: instance real,
/// validation layers reais, GPUs físicas enumeradas com motivo de rejeição
/// por candidata, device real, filas reais, surface (Headless nesta fase —
/// Xcb/Wayland/Android entregues pelas fases de plataforma), swapchain
/// real com recriação, render pass clássico (ADR-037), buffers com upload
/// por staging REAL, SPIR-V validado, submissão e apresentação reais.
///
/// NENHUM Fake/Mock: se o loader/ICD não existir, o probe/reporta
/// honestamente (missão §11/§47 — este backend nunca é simulado).

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "eng/rhi/RhiBackend.hpp"
#include "eng/rhi/Types.hpp"
#include "eng/rhi/vulkan/VulkanInternal.hpp"
#include "eng/rhi/vulkan/VulkanLoader.hpp"

namespace eng::rhi::vulkan {

/// Diagnóstico do backend (para testes de hardware — números REAIS).
struct VulkanStats {
    std::uint64_t framesSubmitted{0};     ///< vkQueueSubmit com sucesso
    std::uint64_t presentsSubmitted{0};    ///< vkQueuePresentKHR chamados
    std::uint64_t presentsOk{0};           ///< apresentações com sucesso
    std::uint32_t swapchainRecreations{0}; ///< recriações (resize/out-of-date)
    std::uint32_t gpusEnumerated{0};       ///< GPUs físicas vistas
    std::uint32_t gpusRejected{0};         ///< rejeitadas por requisitos
    bool validationLayerActive{false};     ///< layer Khronos de fato ativa
    std::string rejectedGpuReasons{};      ///< motivo por candidata (log)
};

/// Fábrica para `Renderer::registerBackend(BackendType::Vulkan, ...)`.
[[nodiscard]] std::unique_ptr<RhiBackend> createBackend();

class VulkanBackend final : public RhiBackend {
public:
    VulkanBackend() = default;
    ~VulkanBackend() override;

    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;

    // --- RhiBackend ------------------------------------------------------------
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

    // --- diagnóstico (testes de hardware) ----------------------------------------
    [[nodiscard]] const VulkanStats& stats() const noexcept { return stats_; }
    [[nodiscard]] VkInstance instanceForDiagnostics() const noexcept { return instance_; }

private:
    struct BufferEntry {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        std::size_t size{0};
        eng::rhi::BufferUsage usage{};
    };
    struct ShaderEntry {
        VkShaderModule vertex{VK_NULL_HANDLE};
        VkShaderModule fragment{VK_NULL_HANDLE};
        std::string debugName{};
    };
    struct PipelineEntry {
        VkPipeline pipeline{VK_NULL_HANDLE};
        VkPipelineLayout layout{VK_NULL_HANDLE};
        eng::rhi::Format colorFormat{eng::rhi::Format::Undefined};
    };
    /// Frame in flight (ADR-035/auditoria F5): um slot por frame pendente.
    struct FrameSlot {
        VkCommandBuffer command{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        VkSemaphore imageAvailable{VK_NULL_HANDLE};
        VkSemaphore renderFinished{VK_NULL_HANDLE};
        bool inFlight{false};
        bool recording{false};
        std::uint64_t frameId{0};
        std::uint32_t imageIndex{0};
        VkFramebuffer framebuffer{VK_NULL_HANDLE};
    };

    // --- helpers internos (definidos nos .cpp correspondentes) ------------------
    void destroyAll() noexcept;
    [[nodiscard]] eng::core::Result<void> requireInitialized() const;
    [[nodiscard]] FrameSlot* findRecordingSlot(std::uint64_t frameId);
    [[nodiscard]] eng::core::Result<std::uint32_t> pickMemoryType(
        std::uint32_t typeBits, VkMemoryPropertyFlags properties,
        const char* what) const;
    [[nodiscard]] eng::core::Result<void> uploadToDeviceLocal(
        BufferEntry& entry, std::size_t offset, std::span<const std::byte> data);
    [[nodiscard]] eng::core::Result<void> createSwapchain(std::uint32_t width,
                                                          std::uint32_t height);
    void destroySwapchain() noexcept;
    [[nodiscard]] eng::core::Result<void> recreateSwapchain();

    // --- estado ------------------------------------------------------------------
    VulkanLibrary library_{};
    VkInstance instance_{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT messenger_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    std::uint32_t graphicsFamily_{0xFFFFFFFFu};
    std::uint32_t presentFamily_{0xFFFFFFFFu};
    VkQueue graphicsQueue_{VK_NULL_HANDLE};
    VkQueue presentQueue_{VK_NULL_HANDLE};
    VkCommandPool commandPool_{VK_NULL_HANDLE};
    VkPhysicalDeviceMemoryProperties memoryProperties_{};

    // --- swapchain/render pass ---------------------------------------------------
    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    std::vector<VkImage> swapchainImages_{};
    std::vector<VkImageView> swapchainViews_{};
    VkFormat swapchainFormat_{VK_FORMAT_UNDEFINED};
    VkExtent2D swapchainExtent_{};
    VkRenderPass renderPass_{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers_{};
    bool swapchainSuboptimal_{false};

    // --- recursos -----------------------------------------------------------------
    HandleTable<BufferEntry> buffers_{};
    HandleTable<ShaderEntry> shaders_{};
    HandleTable<PipelineEntry> pipelines_{};

    // --- frames ---------------------------------------------------------------------
    std::vector<FrameSlot> frameSlots_{};
    std::uint64_t nextFrameId_{0};
    std::size_t acquireSlot_{0};
    struct PendingPresent {
        std::uint32_t imageIndex{0};
        VkSemaphore renderFinished{VK_NULL_HANDLE};
    };
    std::vector<PendingPresent> pendingPresents_{};

    // --- estado geral ------------------------------------------------------------------
    bool initialized_{false};
    bool hasSurface_{false};
    bool surfaceLost_{false};
    RendererCapabilities capabilities_{};
    VulkanStats stats_{};
};

} // namespace eng::rhi::vulkan
