/// FakeBackend — implementação (TESTES APENAS, missão §11). Ver FakeBackend.hpp.

#include "FakeBackend.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace eng::rhi::testing {
namespace {

eng::core::Error err(std::string_view message) {
    return eng::core::Error{eng::core::StatusCode::InvalidArgument, std::string{message}};
}

// ---- estado estático de rastreio (single-threaded por convenção) -----------

std::vector<FakeBackend::Scenario>& scenarioQueue() {
    static std::vector<FakeBackend::Scenario> queue;
    return queue;
}
std::vector<FakeBackend*>& aliveInstancesRef() {
    static std::vector<FakeBackend*> instances;
    return instances;
}
FakeBackend*& lastCreatedRef() {
    static FakeBackend* last = nullptr;
    return last;
}
FakeBackend::TearDown& lastTearDownRef() {
    static FakeBackend::TearDown snapshot{};
    return snapshot;
}

/// Decodifica índice/geração de um handle; false se nulo.
bool decodeHandle(std::uint64_t id, std::uint32_t& outIndex, std::uint32_t& outGeneration) {
    if (id == 0u) {
        return false;
    }
    outIndex = static_cast<std::uint32_t>((id & 0xFFFFFFFFu) - 1u);
    outGeneration = static_cast<std::uint32_t>(id >> 32);
    return true;
}

} // namespace

std::unique_ptr<RhiBackend> FakeBackend::create() {
    // O cenário em fila define o comportamento DESTA instância.
    auto& queue = scenarioQueue();
    FakeBackend* backend = new FakeBackend();
    if (!queue.empty()) {
        backend->scenario_ = queue.front();
        queue.erase(queue.begin());
    }
    return std::unique_ptr<RhiBackend>(backend);
}

void FakeBackend::queueScenario(Scenario scenario) {
    scenarioQueue().push_back(std::move(scenario));
}

void FakeBackend::resetTestState() {
    scenarioQueue().clear();
    aliveInstancesRef().clear();
    lastCreatedRef() = nullptr;
    lastTearDownRef() = TearDown{};
}

FakeBackend* FakeBackend::lastCreated() {
    return lastCreatedRef();
}

std::vector<FakeBackend*> FakeBackend::aliveInstances() {
    return aliveInstancesRef();
}

FakeBackend::TearDown FakeBackend::lastTearDown() {
    return lastTearDownRef();
}

void FakeBackend::setNextAcquire(FrameAcquireStatus status) {
    nextAcquire_ = status;
}

void FakeBackend::setSurfaceLost(bool lost) {
    surfaceLost_ = lost;
}

FakeBackend::FakeBackend() {
    aliveInstancesRef().push_back(this);
    lastCreatedRef() = this;
}

FakeBackend::~FakeBackend() {
    auto& instances = aliveInstancesRef();
    const auto it = std::find(instances.begin(), instances.end(), this);
    if (it != instances.end()) {
        instances.erase(it);
    }
    if (lastCreatedRef() == this) {
        lastCreatedRef() = instances.empty() ? nullptr : instances.back();
    }
    lastTearDownRef() =
        TearDown{liveResources(), hasActiveFrame(), initialized_};
}

std::size_t FakeBackend::liveResources() const noexcept {
    return buffers_.size() + shaders_.size() + pipelines_.size();
}

std::vector<std::byte> FakeBackend::bufferData(BufferHandle handle) const {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    if (!decodeHandle(handle.id, index, generation)) {
        return {};
    }
    const auto it = buffers_.find(index);
    if (it == buffers_.end() || it->second.generation != generation) {
        return {};
    }
    return it->second.data;
}

eng::core::Result<void> FakeBackend::requireInitialized() const {
    if (!initialized_) {
        return eng::core::makeUnexpected(
            err("rhi.fake: operação antes de initialize()"));
    }
    return {};
}

eng::core::Result<void> FakeBackend::requireRecording(std::uint64_t frameId) const {
    if (phase_ != Phase::Recording || frameId != activeFrameId_ || frameId == 0) {
        return eng::core::makeUnexpected(
            err("rhi.fake: frameId inválido (sessão inativa ou já submetida)"));
    }
    return {};
}

void FakeBackend::record(std::string op) {
    ops_.push_back(std::move(op));
}

// =============================================================================
// Ciclo de vida
// =============================================================================

BackendProbe FakeBackend::probe() {
    return scenario_.probe;
}

eng::core::Result<void> FakeBackend::initialize(const RendererConfig& config,
                                                RendererCapabilities& outCapabilities) {
    if (scenario_.failInitialize) {
        return eng::core::makeUnexpected(
            eng::core::Error{eng::core::StatusCode::NotSupported,
                             scenario_.initializeMessage});
    }
    initialized_ = true;
    hasSurface_ = config.surface.isValid();

    outCapabilities = RendererCapabilities{};
    outCapabilities.backendName = "Fake (TESTES)";
    outCapabilities.apiVersion = "0.0-test";
    outCapabilities.device = DeviceInfo{"FakeDevice", "FakeVendor", "FakeDriver 0",
                                        "0.0", DeviceKind::Cpu};
    outCapabilities.softwareRendering = true;  // nunca reportado como real
    outCapabilities.maxTextureSize = 1024;
    outCapabilities.maxVertexAttributes = 16;
    outCapabilities.maxColorAttachments = 4;
    outCapabilities.maxUniformBufferSize = 4096;
    outCapabilities.instancing = true;
    outCapabilities.compute = true;
    outCapabilities.multisample = true;
    outCapabilities.wireframe = false;
    outCapabilities.supportedFormats = {Format::R8G8B8A8Unorm, Format::B8G8R8A8Srgb,
                                        Format::D32Sfloat};
    outCapabilities.presentation = hasSurface_;
    outCapabilities.validationState =
        config.enableValidation
            ? (scenario_.validationAvailable ? ValidationState::Enabled
                                             : ValidationState::Unavailable)
            : ValidationState::DisabledByConfiguration;

    capabilities_ = outCapabilities;
    record("init");
    return {};
}

const RendererCapabilities& FakeBackend::capabilities() const {
    return capabilities_;
}

// =============================================================================
// Recursos
// =============================================================================

eng::core::Result<BufferHandle> FakeBackend::createBuffer(const BufferDesc& desc) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    if (desc.size == 0) {
        return eng::core::makeUnexpected(err("rhi.fake.buffer: tamanho zero"));
    }
    // Reuso de slot com geração nova — stale handles são detectados.
    std::uint32_t index = 0;
    std::uint32_t generation = 1;
    if (!freeBufferSlots_.empty()) {
        const auto it = freeBufferSlots_.begin();
        index = it->first;
        generation = it->second + 1;
        freeBufferSlots_.erase(it);
    } else {
        index = nextBufferIndex_++;
    }
    BufferEntry entry{};
    entry.generation = generation;
    entry.size = desc.size;
    entry.usage = desc.usage;
    entry.data.assign(desc.initialData.begin(), desc.initialData.end());
    entry.data.resize(desc.size, std::byte{0});
    buffers_.emplace(index, std::move(entry));
    record("createBuffer:" + std::to_string(desc.size));
    return BufferHandle{encodeHandle(index, generation)};
}

eng::core::Result<ShaderHandle> FakeBackend::createShader(const ShaderDesc& desc) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    const bool anyRepresentation = !desc.vertexSpirv.empty() || !desc.fragmentSpirv.empty() ||
                                   !desc.vertexGlsl.empty() || !desc.fragmentGlsl.empty();
    if (!anyRepresentation) {
        return eng::core::makeUnexpected(
            err("rhi.fake.shader: sem representação (SPIR-V e GLSL vazios)"));
    }
    std::uint32_t index = 0;
    std::uint32_t generation = 1;
    if (!freeShaderSlots_.empty()) {
        const auto it = freeShaderSlots_.begin();
        index = it->first;
        generation = it->second + 1;
        freeShaderSlots_.erase(it);
    } else {
        index = nextShaderIndex_++;
    }
    shaders_.emplace(index, ShaderEntry{generation});
    record("createShader:" + std::string{desc.debugName.empty() ? "?" : desc.debugName});
    return ShaderHandle{encodeHandle(index, generation)};
}

eng::core::Result<GraphicsPipelineHandle> FakeBackend::createGraphicsPipeline(
    const GraphicsPipelineDesc& desc) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    if (!decodeHandle(desc.shader.id, index, generation) ||
        shaders_.find(index) == shaders_.end() ||
        shaders_.at(index).generation != generation) {
        return eng::core::makeUnexpected(
            err("rhi.fake.pipeline: shader nulo/stale no desc"));
    }
    std::uint32_t pipelineIndex = 0;
    std::uint32_t pipelineGeneration = 1;
    if (!freePipelineSlots_.empty()) {
        const auto it = freePipelineSlots_.begin();
        pipelineIndex = it->first;
        pipelineGeneration = it->second + 1;
        freePipelineSlots_.erase(it);
    } else {
        pipelineIndex = nextPipelineIndex_++;
    }
    pipelines_.emplace(pipelineIndex, PipelineEntry{pipelineGeneration, desc.shader});
    record("createPipeline");
    return GraphicsPipelineHandle{encodeHandle(pipelineIndex, pipelineGeneration)};
}

eng::core::Result<void> FakeBackend::updateBuffer(BufferHandle handle, std::size_t offset,
                                                  std::span<const std::byte> data) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    if (!decodeHandle(handle.id, index, generation)) {
        return eng::core::makeUnexpected(err("rhi.fake.buffer: handle nulo"));
    }
    const auto it = buffers_.find(index);
    if (it == buffers_.end() || it->second.generation != generation) {
        return eng::core::makeUnexpected(
            err("rhi.fake.buffer: handle stale/destruído"));
    }
    if (offset > it->second.size || data.size() > it->second.size - offset) {
        return eng::core::makeUnexpected(err("rhi.fake.buffer: update fora dos limites"));
    }
    std::copy(data.begin(), data.end(), it->second.data.begin() +
                                          static_cast<std::ptrdiff_t>(offset));
    record("update:" + std::to_string(offset) + ":" + std::to_string(data.size()));
    return {};
}

eng::core::Result<void> FakeBackend::destroyBuffer(BufferHandle handle) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    if (!decodeHandle(handle.id, index, generation)) {
        return eng::core::makeUnexpected(err("rhi.fake.buffer: handle nulo em destroy"));
    }
    const auto it = buffers_.find(index);
    if (it == buffers_.end()) {
        return eng::core::makeUnexpected(err("rhi.fake.buffer: double-destroy"));
    }
    if (it->second.generation != generation) {
        return eng::core::makeUnexpected(err("rhi.fake.buffer: handle stale em destroy"));
    }
    freeBufferSlots_.emplace(index, it->second.generation);
    buffers_.erase(it);
    record("destroyBuffer");
    return {};
}

eng::core::Result<void> FakeBackend::destroyShader(ShaderHandle handle) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    if (!decodeHandle(handle.id, index, generation)) {
        return eng::core::makeUnexpected(err("rhi.fake.shader: handle nulo em destroy"));
    }
    const auto it = shaders_.find(index);
    if (it == shaders_.end()) {
        return eng::core::makeUnexpected(err("rhi.fake.shader: double-destroy"));
    }
    if (it->second.generation != generation) {
        return eng::core::makeUnexpected(err("rhi.fake.shader: handle stale em destroy"));
    }
    freeShaderSlots_.emplace(index, it->second.generation);
    shaders_.erase(it);
    record("destroyShader");
    return {};
}

eng::core::Result<void> FakeBackend::destroyGraphicsPipeline(GraphicsPipelineHandle handle) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    if (!decodeHandle(handle.id, index, generation)) {
        return eng::core::makeUnexpected(
            err("rhi.fake.pipeline: handle nulo em destroy"));
    }
    const auto it = pipelines_.find(index);
    if (it == pipelines_.end()) {
        return eng::core::makeUnexpected(err("rhi.fake.pipeline: double-destroy"));
    }
    if (it->second.generation != generation) {
        return eng::core::makeUnexpected(err("rhi.fake.pipeline: handle stale em destroy"));
    }
    freePipelineSlots_.emplace(index, it->second.generation);
    pipelines_.erase(it);
    record("destroyPipeline");
    return {};
}

// =============================================================================
// Frame
// =============================================================================

eng::core::Result<BeginFrameResult> FakeBackend::beginFrame() {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    if (!hasSurface_) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::NotSupported,
            "rhi.fake: beginFrame sem surface (modo device-only — missão §13)"});
    }
    if (surfaceLost_) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::NotSupported,
            "rhi.fake.surface: perdida (recrie via resize ou novo Renderer)"});
    }
    if (phase_ == Phase::Recording) {
        return eng::core::makeUnexpected(
            err("rhi.fake: frame anterior não finalizado (uma sessão de gravação "
                "por vez; submetido-aguardando-present é aceitável)"));
    }
    if (nextAcquire_.has_value()) {
        const FrameAcquireStatus status = *nextAcquire_;
        nextAcquire_.reset();
        if (status == FrameAcquireStatus::OutOfDate) {
            recreateCount_++;  // recriação simulada; próximo begin é Renderable
            record("begin:outOfDate");
            return BeginFrameResult{status, 0};
        }
        record("begin:minimized");
        return BeginFrameResult{status, 0};
    }
    phase_ = Phase::Recording;
    presentable_ = false;
    pipelineSet_ = false;
    vertexBound_ = false;
    indexBound_ = false;
    ++activeFrameId_;
    record("begin");
    return BeginFrameResult{FrameAcquireStatus::Renderable, activeFrameId_};
}

eng::core::Result<void> FakeBackend::frameClear(std::uint64_t frameId,
                                                 const ClearDesc& /*clear*/) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    if (auto recording = requireRecording(frameId); !recording) {
        return eng::core::makeUnexpected(recording.error());
    }
    record("clear");
    return {};
}

eng::core::Result<void> FakeBackend::frameSetViewport(std::uint64_t frameId,
                                                       const Viewport& /*viewport*/) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    if (auto recording = requireRecording(frameId); !recording) {
        return eng::core::makeUnexpected(recording.error());
    }
    record("viewport");
    return {};
}

eng::core::Result<void> FakeBackend::frameSetPipeline(std::uint64_t frameId,
                                                      GraphicsPipelineHandle pipeline) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    if (auto recording = requireRecording(frameId); !recording) {
        return eng::core::makeUnexpected(recording.error());
    }
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    if (!decodeHandle(pipeline.id, index, generation) ||
        pipelines_.find(index) == pipelines_.end() ||
        pipelines_.at(index).generation != generation) {
        return eng::core::makeUnexpected(
            err("rhi.fake.frame: pipeline nulo/stale em setPipeline"));
    }
    pipelineSet_ = true;
    record("pipeline");
    return {};
}

eng::core::Result<void> FakeBackend::frameBindVertexBuffer(std::uint64_t frameId,
                                                           BufferHandle buffer) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    if (auto recording = requireRecording(frameId); !recording) {
        return eng::core::makeUnexpected(recording.error());
    }
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    if (!decodeHandle(buffer.id, index, generation) || buffers_.find(index) == buffers_.end() ||
        buffers_.at(index).generation != generation) {
        return eng::core::makeUnexpected(
            err("rhi.fake.frame: buffer nulo/stale em bindVertexBuffer"));
    }
    if ((buffers_.at(index).usage & BufferUsage::Vertex) == BufferUsage::None) {
        return eng::core::makeUnexpected(
            err("rhi.fake.frame: buffer sem uso Vertex em bindVertexBuffer"));
    }
    vertexBound_ = true;
    record("vbo");
    return {};
}

eng::core::Result<void> FakeBackend::frameBindIndexBuffer(std::uint64_t frameId,
                                                           BufferHandle buffer,
                                                           IndexType /*indexType*/) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    if (auto recording = requireRecording(frameId); !recording) {
        return eng::core::makeUnexpected(recording.error());
    }
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    if (!decodeHandle(buffer.id, index, generation) || buffers_.find(index) == buffers_.end() ||
        buffers_.at(index).generation != generation) {
        return eng::core::makeUnexpected(
            err("rhi.fake.frame: buffer nulo/stale em bindIndexBuffer"));
    }
    if ((buffers_.at(index).usage & BufferUsage::Index) == BufferUsage::None) {
        return eng::core::makeUnexpected(
            err("rhi.fake.frame: buffer sem uso Index em bindIndexBuffer"));
    }
    indexBound_ = true;
    record("ibo");
    return {};
}

eng::core::Result<void> FakeBackend::frameDraw(std::uint64_t frameId,
                                                std::uint32_t vertexCount,
                                                std::uint32_t /*firstVertex*/) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    if (auto recording = requireRecording(frameId); !recording) {
        return eng::core::makeUnexpected(recording.error());
    }
    if (vertexCount == 0) {
        return eng::core::makeUnexpected(err("rhi.fake.frame: draw com vertexCount zero"));
    }
    if (!pipelineSet_) {
        return eng::core::makeUnexpected(err("rhi.fake.frame: draw sem pipeline"));
    }
    if (!vertexBound_) {
        return eng::core::makeUnexpected(err("rhi.fake.frame: draw sem vertex buffer"));
    }
    record("draw:" + std::to_string(vertexCount));
    return {};
}

eng::core::Result<void> FakeBackend::frameDrawIndexed(std::uint64_t frameId,
                                                      std::uint32_t indexCount,
                                                      std::uint32_t /*firstIndex*/) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    if (auto recording = requireRecording(frameId); !recording) {
        return eng::core::makeUnexpected(recording.error());
    }
    if (indexCount == 0) {
        return eng::core::makeUnexpected(
            err("rhi.fake.frame: drawIndexed com indexCount zero"));
    }
    if (!pipelineSet_ || !vertexBound_ || !indexBound_) {
        return eng::core::makeUnexpected(
            err("rhi.fake.frame: drawIndexed exige pipeline+vbo+ibo"));
    }
    record("drawIndexed:" + std::to_string(indexCount));
    return {};
}

eng::core::Result<void> FakeBackend::endFrame(std::uint64_t frameId) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    if (frameId == 0 || frameId != activeFrameId_ || phase_ != Phase::Recording) {
        return eng::core::makeUnexpected(
            err("rhi.fake: endFrame sem sessão ativa (frameId inválido/end duplo)"));
    }
    phase_ = Phase::Ended;
    presentable_ = true;
    record("end");
    return {};
}

eng::core::Result<void> FakeBackend::present() {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    if (!presentable_) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::NotSupported,
            "rhi.fake: present sem frame submetido (missão §12)"});
    }
    presentable_ = false;
    phase_ = Phase::Idle;
    ++presentCount_;
    record("present");
    return {};
}

eng::core::Result<void> FakeBackend::resize(std::uint32_t width, std::uint32_t height) {
    if (auto ready = requireInitialized(); !ready) {
        return eng::core::makeUnexpected(ready.error());
    }
    if (!hasSurface_) {
        return eng::core::makeUnexpected(
            eng::core::Error{eng::core::StatusCode::NotSupported,
                             "rhi.fake: resize sem surface (device-only)"});
    }
    ++resizeCount_;
    surfaceLost_ = false;  // recriação restaura a surface perdida
    record("resize:" + std::to_string(width) + "x" + std::to_string(height));
    return {};
}

bool FakeBackend::surfaceLost() const {
    return surfaceLost_;
}

} // namespace eng::rhi::testing
