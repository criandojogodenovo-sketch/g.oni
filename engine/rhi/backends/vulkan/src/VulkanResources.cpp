/// Backend Vulkan — recursos: buffers (staging REAL), shaders SPIR-V
/// validados, pipelines com render pass clássico (FASE 5, missão §24–§27).

#include <cstring>
#include <utility>

#include "eng/rhi/vulkan/VulkanBackend.hpp"
// (Sem ENG_LOG_CATEGORY: este TU não loga — declará-lo sem uso é warning
// no clang [-Wunused-const-variable]; a política de zero-warnings manda
// declarar a categoria apenas onde ela é usada.)

namespace eng::rhi::vulkan {
namespace {

using eng::core::Result;
using eng::core::StatusCode;
using eng::rhi::BufferUsage;

[[nodiscard]] eng::core::Error vkErr(StatusCode code, std::string_view what, VkResult result) {
    return eng::core::Error{code,
                            std::string{what} + " (" + vkResultName(result) + ")"};
}

[[nodiscard]] VkBufferUsageFlags toVkBufferUsage(BufferUsage usage) noexcept {
    VkBufferUsageFlags flags = 0;
    if ((usage & BufferUsage::Vertex) != BufferUsage::None) {
        flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if ((usage & BufferUsage::Index) != BufferUsage::None) {
        flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if ((usage & BufferUsage::Uniform) != BufferUsage::None) {
        flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if ((usage & BufferUsage::Storage) != BufferUsage::None) {
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    // Todo buffer do backend recebe uploads por staging (ADR-037): destino
    // de transferência + origem para a cópia staging→device.
    flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return flags;
}

[[nodiscard]] VkCullModeFlags toVkCullMode(eng::rhi::CullMode mode) noexcept {
    switch (mode) {
    case eng::rhi::CullMode::None: return VK_CULL_MODE_NONE;
    case eng::rhi::CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    case eng::rhi::CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    }
    return VK_CULL_MODE_NONE;
}

[[nodiscard]] VkFrontFace toVkFrontFace(eng::rhi::FrontFace face) noexcept {
    return face == eng::rhi::FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE
                                                  : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

[[nodiscard]] VkPolygonMode toVkPolygonMode(eng::rhi::FillMode mode,
                                            bool& outNeedsNonSolidFeature) noexcept {
    outNeedsNonSolidFeature = false;
    switch (mode) {
    case eng::rhi::FillMode::Solid: return VK_POLYGON_MODE_FILL;
    case eng::rhi::FillMode::Wireframe:
        outNeedsNonSolidFeature = true;
        return VK_POLYGON_MODE_LINE;
    }
    return VK_POLYGON_MODE_FILL;
}

[[nodiscard]] VkCompareOp toVkCompareOp(eng::rhi::CompareOp op) noexcept {
    using O = eng::rhi::CompareOp;
    switch (op) {
    case O::Never: return VK_COMPARE_OP_NEVER;
    case O::Less: return VK_COMPARE_OP_LESS;
    case O::Equal: return VK_COMPARE_OP_EQUAL;
    case O::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case O::Greater: return VK_COMPARE_OP_GREATER;
    case O::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
    case O::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case O::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS;
}

[[nodiscard]] VkBlendFactor toVkBlendFactor(eng::rhi::BlendFactor factor) noexcept {
    using F = eng::rhi::BlendFactor;
    switch (factor) {
    case F::Zero: return VK_BLEND_FACTOR_ZERO;
    case F::One: return VK_BLEND_FACTOR_ONE;
    case F::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
    case F::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case F::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
    case F::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

[[nodiscard]] VkBlendOp toVkBlendOp(eng::rhi::BlendOp op) noexcept {
    using O = eng::rhi::BlendOp;
    switch (op) {
    case O::Add: return VK_BLEND_OP_ADD;
    case O::Subtract: return VK_BLEND_OP_SUBTRACT;
    case O::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case O::Min: return VK_BLEND_OP_MIN;
    case O::Max: return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

} // namespace

// =============================================================================
// Buffers (missão §25): DEVICE_LOCAL + upload por staging REAL
// =============================================================================

Result<std::uint32_t> VulkanBackend::pickMemoryType(std::uint32_t typeBits,
                                                    VkMemoryPropertyFlags properties,
                                                    const char* what) const {
    for (std::uint32_t type = 0; type < memoryProperties_.memoryTypeCount; ++type) {
        if ((typeBits & (1u << type)) != 0u &&
            (memoryProperties_.memoryTypes[type].propertyFlags & properties) == properties) {
            return type;
        }
    }
    return eng::core::makeUnexpected(makeError(
        StatusCode::OutOfMemory,
        std::string{"rhi.vulkan: sem memory type para "} + what +
            " (flags exigidas não disponíveis)"));
}

Result<void> VulkanBackend::uploadToDeviceLocal(VulkanBackend::BufferEntry& entry,
                                                 std::size_t offset,
                                                 std::span<const std::byte> data) {
    const auto& fn = library_.functions();
    if (data.empty()) {
        return {};
    }

    // 1) Buffer de staging HOST_VISIBLE|HOST_COHERENT com o conteúdo.
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = data.size();
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkResult result = fn.vkCreateBuffer(device_, &stagingInfo, nullptr, &staging);
    if (result != VK_SUCCESS) {
        return eng::core::makeUnexpected(vkErr(StatusCode::Unknown, "rhi.vulkan: staging", result));
    }
    VkMemoryRequirements stagingRequirements{};
    fn.vkGetBufferMemoryRequirements(device_, staging, &stagingRequirements);
    auto stagingType = pickMemoryType(stagingRequirements.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      "staging");
    if (!stagingType) {
        fn.vkDestroyBuffer(device_, staging, nullptr);
        return eng::core::makeUnexpected(stagingType.error());
    }
    VkMemoryAllocateInfo stagingAlloc{};
    stagingAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAlloc.allocationSize = stagingRequirements.size;
    stagingAlloc.memoryTypeIndex = stagingType.value();
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    result = fn.vkAllocateMemory(device_, &stagingAlloc, nullptr, &stagingMemory);
    if (result != VK_SUCCESS) {
        fn.vkDestroyBuffer(device_, staging, nullptr);
        return eng::core::makeUnexpected(vkErr(StatusCode::OutOfMemory, "rhi.vulkan: staging", result));
    }
    result = fn.vkBindBufferMemory(device_, staging, stagingMemory, 0);
    if (result != VK_SUCCESS) {
        fn.vkDestroyBuffer(device_, staging, nullptr);
        fn.vkFreeMemory(device_, stagingMemory, nullptr);
        return eng::core::makeUnexpected(vkErr(StatusCode::Unknown, "rhi.vulkan: staging bind", result));
    }
    void* mapped = nullptr;
    result = fn.vkMapMemory(device_, stagingMemory, 0, data.size(), 0, &mapped);
    if (result != VK_SUCCESS) {
        fn.vkDestroyBuffer(device_, staging, nullptr);
        fn.vkFreeMemory(device_, stagingMemory, nullptr);
        return eng::core::makeUnexpected(vkErr(StatusCode::Unknown, "rhi.vulkan: staging map", result));
    }
    std::memcpy(mapped, data.data(), data.size());
    fn.vkUnmapMemory(device_, stagingMemory);

    // 2) Comando de cópia em buffer dedicado + fence própria (espera REAL).
    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = commandPool_;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    result = fn.vkAllocateCommandBuffers(device_, &commandInfo, &command);
    if (result != VK_SUCCESS) {
        fn.vkDestroyBuffer(device_, staging, nullptr);
        fn.vkFreeMemory(device_, stagingMemory, nullptr);
        return eng::core::makeUnexpected(vkErr(StatusCode::Unknown, "rhi.vulkan: staging cmd", result));
    }
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    result = fn.vkCreateFence(device_, &fenceInfo, nullptr, &fence);
    if (result != VK_SUCCESS) {
        fn.vkDestroyBuffer(device_, staging, nullptr);
        fn.vkFreeMemory(device_, stagingMemory, nullptr);
        return eng::core::makeUnexpected(vkErr(StatusCode::Unknown, "rhi.vulkan: staging fence", result));
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    fn.vkBeginCommandBuffer(command, &beginInfo);
    VkBufferCopy region{};
    region.srcOffset = 0;
    region.dstOffset = offset;
    region.size = data.size();
    fn.vkCmdCopyBuffer(command, staging, entry.buffer, 1, &region);
    // O fence espera a cópia CONCLUIR: write visible para submits futuros.
    fn.vkEndCommandBuffer(command);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &command;
    result = fn.vkQueueSubmit(graphicsQueue_, 1, &submitInfo, fence);
    if (result == VK_SUCCESS) {
        result = fn.vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    }
    fn.vkDestroyFence(device_, fence, nullptr);
    fn.vkFreeCommandBuffers(device_, commandPool_, 1, &command);
    fn.vkDestroyBuffer(device_, staging, nullptr);
    fn.vkFreeMemory(device_, stagingMemory, nullptr);
    if (result != VK_SUCCESS) {
        return eng::core::makeUnexpected(
            vkErr(StatusCode::Unknown, "rhi.vulkan: upload staging submit/wait", result));
    }
    return {};
}

Result<BufferHandle> VulkanBackend::createBuffer(const BufferDesc& desc) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    if (desc.size == 0) {
        return eng::core::makeUnexpected(
            makeError(StatusCode::InvalidArgument, "rhi.vulkan.buffer: tamanho zero"));
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = toVkBufferUsage(desc.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    BufferEntry entry{};
    entry.size = desc.size;
    entry.usage = desc.usage;
    VkResult result =
        library_.functions().vkCreateBuffer(device_, &bufferInfo, nullptr, &entry.buffer);
    if (result != VK_SUCCESS) {
        return eng::core::makeUnexpected(
            vkErr(StatusCode::Unknown, "rhi.vulkan.buffer: vkCreateBuffer", result));
    }
    VkMemoryRequirements requirements{};
    library_.functions().vkGetBufferMemoryRequirements(device_, entry.buffer, &requirements);
    auto memoryType = pickMemoryType(requirements.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "vertex/index");
    if (!memoryType) {
        library_.functions().vkDestroyBuffer(device_, entry.buffer, nullptr);
        return eng::core::makeUnexpected(memoryType.error());
    }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = memoryType.value();
    result = library_.functions().vkAllocateMemory(device_, &allocInfo, nullptr, &entry.memory);
    if (result != VK_SUCCESS) {
        library_.functions().vkDestroyBuffer(device_, entry.buffer, nullptr);
        return eng::core::makeUnexpected(
            vkErr(StatusCode::OutOfMemory, "rhi.vulkan.buffer: vkAllocateMemory", result));
    }
    result = library_.functions().vkBindBufferMemory(device_, entry.buffer, entry.memory, 0);
    if (result != VK_SUCCESS) {
        library_.functions().vkDestroyBuffer(device_, entry.buffer, nullptr);
        library_.functions().vkFreeMemory(device_, entry.memory, nullptr);
        return eng::core::makeUnexpected(
            vkErr(StatusCode::Unknown, "rhi.vulkan.buffer: vkBindBufferMemory", result));
    }

    // Upload inicial REAL (staging) quando há dados.
    if (!desc.initialData.empty()) {
        if (desc.initialData.size() > desc.size) {
            library_.functions().vkDestroyBuffer(device_, entry.buffer, nullptr);
            library_.functions().vkFreeMemory(device_, entry.memory, nullptr);
            return eng::core::makeUnexpected(makeError(
                StatusCode::InvalidArgument, "rhi.vulkan.buffer: initialData > size"));
        }
        const auto uploaded = uploadToDeviceLocal(entry, 0, desc.initialData);
        if (!uploaded) {
            library_.functions().vkDestroyBuffer(device_, entry.buffer, nullptr);
            library_.functions().vkFreeMemory(device_, entry.memory, nullptr);
            return eng::core::makeUnexpected(uploaded.error());
        }
    }
    return BufferHandle{buffers_.insert(std::move(entry))};
}

Result<void> VulkanBackend::updateBuffer(BufferHandle handle, std::size_t offset,
                                          std::span<const std::byte> data) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    BufferEntry* entry = buffers_.find(handle.id);
    if (entry == nullptr) {
        return eng::core::makeUnexpected(
            makeError(StatusCode::InvalidArgument, "rhi.vulkan.buffer: handle nulo/stale"));
    }
    if (offset > entry->size || data.size() > entry->size - offset) {
        return eng::core::makeUnexpected(makeError(
            StatusCode::InvalidArgument,
            "rhi.vulkan.buffer: update fora dos limites (offset=" + std::to_string(offset) +
                " tamanho=" + std::to_string(data.size()) + " buffer=" +
                std::to_string(entry->size) + ")"));
    }
    return uploadToDeviceLocal(*entry, offset, data);
}

Result<void> VulkanBackend::destroyBuffer(BufferHandle handle) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    BufferEntry entry{};
    if (!buffers_.remove(handle.id, entry)) {
        return eng::core::makeUnexpected(
            makeError(StatusCode::InvalidArgument, "rhi.vulkan.buffer: handle nulo/stale/double"));
    }
    library_.functions().vkDeviceWaitIdle(device_);  // simples e correto nesta escala (ADR-037)
    library_.functions().vkDestroyBuffer(device_, entry.buffer, nullptr);
    library_.functions().vkFreeMemory(device_, entry.memory, nullptr);
    return {};
}

// =============================================================================
// Shaders (missão §27): SPIR-V validado (magic/tamanho) — nenhuma simulação
// =============================================================================

Result<ShaderHandle> VulkanBackend::createShader(const ShaderDesc& desc) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    if (desc.vertexSpirv.empty() || desc.fragmentSpirv.empty()) {
        return eng::core::makeUnexpected(makeError(
            StatusCode::NotSupported,
            "rhi.vulkan.shader: backend Vulkan exige SPIR-V (vertexSpirv e "
            "fragmentSpirv) — a representação GLSL é do backend GLES"));
    }
    std::string error{};
    if (!spirvLooksValid(reinterpret_cast<const unsigned char*>(desc.vertexSpirv.data()),
                         desc.vertexSpirv.size(), error)) {
        return eng::core::makeUnexpected(
            makeError(StatusCode::InvalidArgument, "rhi.vulkan.shader: vertex: " + error));
    }
    if (!spirvLooksValid(reinterpret_cast<const unsigned char*>(desc.fragmentSpirv.data()),
                         desc.fragmentSpirv.size(), error)) {
        return eng::core::makeUnexpected(
            makeError(StatusCode::InvalidArgument, "rhi.vulkan.shader: fragment: " + error));
    }

    ShaderEntry entry{};
    entry.debugName = std::string{desc.debugName};
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = desc.vertexSpirv.size();
    moduleInfo.pCode = reinterpret_cast<const std::uint32_t*>(desc.vertexSpirv.data());
    VkResult result =
        library_.functions().vkCreateShaderModule(device_, &moduleInfo, nullptr, &entry.vertex);
    if (result != VK_SUCCESS) {
        return eng::core::makeUnexpected(
            vkErr(StatusCode::InvalidArgument, "rhi.vulkan.shader: vkCreateShaderModule (vertex)",
                  result));
    }
    moduleInfo.codeSize = desc.fragmentSpirv.size();
    moduleInfo.pCode = reinterpret_cast<const std::uint32_t*>(desc.fragmentSpirv.data());
    result =
        library_.functions().vkCreateShaderModule(device_, &moduleInfo, nullptr, &entry.fragment);
    if (result != VK_SUCCESS) {
        library_.functions().vkDestroyShaderModule(device_, entry.vertex, nullptr);
        return eng::core::makeUnexpected(vkErr(
            StatusCode::InvalidArgument, "rhi.vulkan.shader: vkCreateShaderModule (fragment)",
            result));
    }
    return ShaderHandle{shaders_.insert(std::move(entry))};
}

Result<void> VulkanBackend::destroyShader(ShaderHandle handle) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    ShaderEntry entry{};
    if (!shaders_.remove(handle.id, entry)) {
        return eng::core::makeUnexpected(
            makeError(StatusCode::InvalidArgument, "rhi.vulkan.shader: handle nulo/stale/double"));
    }
    library_.functions().vkDeviceWaitIdle(device_);
    library_.functions().vkDestroyShaderModule(device_, entry.vertex, nullptr);
    library_.functions().vkDestroyShaderModule(device_, entry.fragment, nullptr);
    return {};
}

// =============================================================================
// Pipeline (missão §24): render pass clássico, viewport/scissor dinâmicos
// =============================================================================

Result<GraphicsPipelineHandle> VulkanBackend::createGraphicsPipeline(
    const GraphicsPipelineDesc& desc) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    ShaderEntry* shader = shaders_.find(desc.shader.id);
    if (shader == nullptr) {
        return eng::core::makeUnexpected(
            makeError(StatusCode::InvalidArgument, "rhi.vulkan.pipeline: shader nulo/stale"));
    }
    // L1 (auditoria F5): Undefined = formato da surface; explícito deve
    // coincidir com o REAL da swapchain (validado abaixo).
    const VkFormat targetFormat = toVkFormat(desc.renderTarget.colorFormat);
    if (desc.renderTarget.colorFormat != eng::rhi::Format::Undefined &&
        (!hasSurface_ || targetFormat != swapchainFormat_)) {
        return eng::core::makeUnexpected(makeError(
            StatusCode::InvalidArgument,
            "rhi.vulkan.pipeline: colorFormat explícito difere do formato real da surface"));
    }
    if (desc.renderTarget.depthFormat != eng::rhi::Format::Undefined) {
        return eng::core::makeUnexpected(makeError(
            StatusCode::NotSupported,
            "rhi.vulkan.pipeline: depth attachment entra com o render-graph futuro "
            "(FASE 5: color-only)"));
    }
    if (desc.depth.test || desc.depth.write) {
        return eng::core::makeUnexpected(makeError(
            StatusCode::InvalidArgument,
            "rhi.vulkan.pipeline: DepthState com render target color-only"));
    }
    bool needsNonSolid = false;
    const VkPolygonMode polygonMode = toVkPolygonMode(desc.raster.fill, needsNonSolid);
    if (needsNonSolid) {
        return eng::core::makeUnexpected(makeError(
            StatusCode::InvalidArgument,
            "rhi.vulkan.pipeline: wireframe exige fillModeNonSolid (não habilitada — "
            "ADR-037)"));
    }

    const auto& fn = library_.functions();
    PipelineEntry entry{};
    entry.colorFormat =
        desc.renderTarget.colorFormat == eng::rhi::Format::Undefined
            ? fromVkFormat(swapchainFormat_)
            : desc.renderTarget.colorFormat;

    // Layout vazio (sem descritores nesta fase — ADR-037).
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkResult result = fn.vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &entry.layout);
    if (result != VK_SUCCESS) {
        return eng::core::makeUnexpected(
            vkErr(StatusCode::Unknown, "rhi.vulkan.pipeline: vkCreatePipelineLayout", result));
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = shader->vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = shader->fragment;
    stages[1].pName = "main";

    // Vertex input a partir do VertexLayout (intenção — missão §37).
    std::vector<VkVertexInputBindingDescription> bindings{};
    bindings.reserve(desc.vertexLayout.bindings.size());
    for (const auto& binding : desc.vertexLayout.bindings) {
        bindings.push_back({binding.binding, binding.stride, VK_VERTEX_INPUT_RATE_VERTEX});
    }
    std::vector<VkVertexInputAttributeDescription> attributes{};
    attributes.reserve(desc.vertexLayout.attributes.size());
    for (const auto& attribute : desc.vertexLayout.attributes) {
        attributes.push_back(
            {attribute.location, attribute.binding, toVkFormat(attribute.format),
             attribute.offset});
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport/scissor DINÂMICOS: pipeline não depende do tamanho da surface.
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = polygonMode;
    rasterization.cullMode = toVkCullMode(desc.raster.cull);
    rasterization.frontFace = toVkFrontFace(desc.raster.front);
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample.minSampleShading = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;  // color-only (validado acima)
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = toVkCompareOp(desc.depth.compare);  // coerente

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = desc.blend.enabled ? VK_TRUE : VK_FALSE;
    blendAttachment.srcColorBlendFactor = toVkBlendFactor(desc.blend.srcColor);
    blendAttachment.dstColorBlendFactor = toVkBlendFactor(desc.blend.dstColor);
    blendAttachment.colorBlendOp = toVkBlendOp(desc.blend.colorOp);
    blendAttachment.srcAlphaBlendFactor = toVkBlendFactor(desc.blend.srcColor);
    blendAttachment.dstAlphaBlendFactor = toVkBlendFactor(desc.blend.dstColor);
    blendAttachment.alphaBlendOp = toVkBlendOp(desc.blend.colorOp);
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = entry.layout;
    pipelineInfo.renderPass = renderPass_;  // render pass clássico compartilhado
    pipelineInfo.subpass = 0;
    result = fn.vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                          &entry.pipeline);
    if (result != VK_SUCCESS) {
        fn.vkDestroyPipelineLayout(device_, entry.layout, nullptr);
        return eng::core::makeUnexpected(vkErr(StatusCode::InvalidArgument,
                                                "rhi.vulkan.pipeline: vkCreateGraphicsPipelines",
                                                result));
    }
    return GraphicsPipelineHandle{pipelines_.insert(std::move(entry))};
}

Result<void> VulkanBackend::destroyGraphicsPipeline(GraphicsPipelineHandle handle) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    PipelineEntry entry{};
    if (!pipelines_.remove(handle.id, entry)) {
        return eng::core::makeUnexpected(makeError(
            StatusCode::InvalidArgument, "rhi.vulkan.pipeline: handle nulo/stale/double"));
    }
    library_.functions().vkDeviceWaitIdle(device_);
    library_.functions().vkDestroyPipeline(device_, entry.pipeline, nullptr);
    library_.functions().vkDestroyPipelineLayout(device_, entry.layout, nullptr);
    return {};
}

} // namespace eng::rhi::vulkan
