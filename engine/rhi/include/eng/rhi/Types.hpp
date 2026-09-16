#pragma once

/// eng::rhi — tipos de dados da interface de hardware gráfico (FASE 4).
///
/// Este header (e TODO o módulo eng::rhi) é deliberadamente AGNÓSTICO de
/// API gráfica: nenhum tipo, enum, handle ou header de Vulkan/OpenGL ES/EGL
/// aparece aqui (missão §4/§14). Backends reais (FASES 5/6) traduzem estes
/// conceitos para o seu modelo próprio — a abstraction representa INTENÇÃO.
///
/// ADR-035 (contrato rhi/backend) e ADR-036 (seleção e disponibilidade).

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eng::rhi {

// =============================================================================
// Seleção e disponibilidade (missão §10/§47)
// =============================================================================

/// Backend gráfico selecionável explicitamente ou por `Auto`.
enum class BackendType : std::uint8_t {
    Auto,       ///< ordem de preferência documentada: Vulkan → OpenGL ES
    Vulkan,     ///< exige validação completa; sem fallback silencioso
    OpenGLES,   ///< idem
};

/// Níveis de disponibilidade/suporte (missão §47). Estados progressivos —
/// um nível pressupõe os anteriores. Hardware tests das FASES 5/6 reportam
/// o nível alcançado; NUNCA se declara um nível não verificado.
enum class Availability : std::uint8_t {
    Unavailable,  ///< loader/biblioteca ausente
    Detected,     ///< loader presente (probe superficial, sem efeitos)
    Available,    ///< instance/contexto criável (validação completa passou)
    Initialized,  ///< device/contexto criado e funcional
    Capable,      ///< features exigidas verificadas
    Presentable,  ///< surface/swapchain utilizável
    Rendering,    ///< frame submetido à GPU
    Validated,    ///< output verificado (readback / validation layers)
};

/// Estado real da camada de validação (missão §18) — nunca mascarado.
enum class ValidationState : std::uint8_t {
    Enabled,                   ///< ativa e funcionando
    DisabledByConfiguration,  ///< o chamador pediu sem validação
    Unavailable,              ///< pedida, mas não existe no ambiente
    FailedToInitialize,       ///< pedida, mas falhou ao inicializar
};

// =============================================================================
// Surface — handle nativo opaco (missão §13/§30)
// =============================================================================

/// Plataforma do handle de janela. `rhi` NUNCA interpreta o ponteiro —
/// apenas o repassa ao backend. A FASE 7 (Android) entregará
/// `Kind::Android` (ANativeWindow*) via eng::platform, sem JNI aqui.
enum class NativeWindowKind : std::uint8_t {
    None,
    Xcb,
    Xlib,
    Wayland,
    Win32,
    Android,
    Headless,  ///< surface sem janela (testes/integração)
};

struct NativeWindowHandle {
    const void* handle{nullptr};  ///< opaco; nunca desreferenciado por rhi
    NativeWindowKind kind{NativeWindowKind::None};

    [[nodiscard]] bool isValid() const noexcept {
        return handle != nullptr && kind != NativeWindowKind::None;
    }
    [[nodiscard]] bool operator==(const NativeWindowHandle&) const = default;
};

// =============================================================================
// Formatos e usos (subsets honestos — backend valida suporte REAL, missão §26)
// =============================================================================

enum class Format : std::uint8_t {
    Undefined,
    R8G8B8A8Unorm,
    B8G8R8A8Unorm,
    R8G8B8A8Srgb,
    B8G8R8A8Srgb,
    R32G32B32A32Sfloat,
    R16G16B16A16Sfloat,
    D32Sfloat,
    D24UnormS8Uint,
};

enum class BufferUsage : std::uint16_t {
    None = 0,
    Vertex = 1 << 0,
    Index = 1 << 1,
    Uniform = 1 << 2,
    Storage = 1 << 3,
    CopySrc = 1 << 4,
    CopyDst = 1 << 5,
};

[[nodiscard]] constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept {
    return static_cast<BufferUsage>(static_cast<std::uint16_t>(a) |
                                    static_cast<std::uint16_t>(b));
}
[[nodiscard]] constexpr BufferUsage operator&(BufferUsage a, BufferUsage b) noexcept {
    return static_cast<BufferUsage>(static_cast<std::uint16_t>(a) &
                                    static_cast<std::uint16_t>(b));
}
constexpr BufferUsage& operator|=(BufferUsage& a, BufferUsage b) noexcept {
    return a = a | b;
}

enum class IndexType : std::uint8_t { Uint16, Uint32 };
enum class CullMode : std::uint8_t { None, Back, Front };
enum class FrontFace : std::uint8_t { CounterClockwise, Clockwise };
enum class FillMode : std::uint8_t { Solid, Wireframe };
enum class CompareOp : std::uint8_t {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};
enum class BlendFactor : std::uint8_t {
    Zero,
    One,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
};
enum class BlendOp : std::uint8_t { Add, Subtract, ReverseSubtract, Min, Max };

// =============================================================================
// Handles opacos e tipados (missão §8)
// =============================================================================

/// Handle opaco de recurso gráfico. `id == 0` é o handle nulo/inválido.
/// A codificação interna (índice + geração, no backend) não é contrato do
/// frontend: o contrato é (a) nulo é inválido; (b) handle desconhecido ou
/// destruído produz ERRO determinístico — nunca UB; (c) handle nunca é um
/// ponteiro — o frontend nunca o desreferencia.
template <typename Tag>
struct Handle {
    std::uint64_t id{0};

    [[nodiscard]] bool isValid() const noexcept { return id != 0u; }
    [[nodiscard]] bool operator==(const Handle&) const = default;
};

struct BufferTag;
struct ShaderTag;
struct GraphicsPipelineTag;

using BufferHandle = Handle<BufferTag>;
using ShaderHandle = Handle<ShaderTag>;
using GraphicsPipelineHandle = Handle<GraphicsPipelineTag>;

// =============================================================================
// Descritores de recursos
// =============================================================================

struct BufferDesc {
    std::size_t size{0};
    BufferUsage usage{BufferUsage::None};
    /// Dados iniciais opcionais — upload na criação; `.size() <= size`.
    std::span<const std::byte> initialData{};
};

/// Shader como DADO (missão §27/§40): a abstraction carrega AMBAS as
/// representações. O backend Vulkan (FASE 5) exige SPIR-V; o backend GLES
/// (FASE 6) exige GLSL ES. A ausência da representação exigida é erro
/// preciso — isso garante que a abstraction não é modelada em torno de
/// nenhuma das duas APIs (paridade, missão §40).
struct ShaderDesc {
    std::string_view debugName{};
    std::span<const std::byte> vertexSpirv{};
    std::span<const std::byte> fragmentSpirv{};
    std::string_view vertexGlsl{};
    std::string_view fragmentGlsl{};
};

struct VertexLayout {
    struct Binding {
        std::uint32_t binding{0};
        std::uint32_t stride{0};
    };
    struct Attribute {
        std::uint32_t location{0};
        std::uint32_t binding{0};
        std::uint32_t offset{0};
        Format format{Format::R8G8B8A8Unorm};
    };
    std::vector<Binding> bindings{};
    std::vector<Attribute> attributes{};
};

/// Intenção de rasterização (missão §37) — cada backend traduz para o seu
/// modelo de estado.
struct RasterState {
    CullMode cull{CullMode::Back};
    FrontFace front{FrontFace::CounterClockwise};
    FillMode fill{FillMode::Solid};
};

struct DepthState {
    bool test{false};
    bool write{false};
    CompareOp compare{CompareOp::Less};
};

struct BlendState {
    bool enabled{false};
    BlendFactor srcColor{BlendFactor::One};
    BlendFactor dstColor{BlendFactor::Zero};
    BlendOp colorOp{BlendOp::Add};
};

struct RenderTargetDesc {
    /// `Undefined` (default) = HERDAR o formato da surface em uso
    /// (auditoria FASE 5, L1). Formato explícito deve coincidir com o real
    /// da surface — o backend valida.
    Format colorFormat{Format::Undefined};
    Format depthFormat{Format::Undefined};
    std::uint32_t sampleCount{1};
};

struct GraphicsPipelineDesc {
    ShaderHandle shader{};
    VertexLayout vertexLayout{};
    RasterState raster{};
    DepthState depth{};
    BlendState blend{};
    RenderTargetDesc renderTarget{};
};

// =============================================================================
// Capabilities e device (valores REAIS — missão §9)
// =============================================================================

enum class DeviceKind : std::uint8_t {
    DiscreteGpu,
    IntegratedGpu,
    VirtualGpu,
    Cpu,
    Unknown,
};

/// Identidade do device em uso — preenchida pelo backend consultando a API
/// REAL (vkEnumeratePhysicalDevices / glGetString, ...). Nunca inventada.
struct DeviceInfo {
    std::string name{};
    std::string vendor{};
    std::string driver{};
    std::string apiVersion{};
    DeviceKind kind{DeviceKind::Unknown};
};

/// Capacidades reais do backend ativo. O backend preenche CONSULTANDO a
/// API; o FakeBackend (somente testes) preenche valores claramente marcados
/// como fake e nunca é reportado como suporte real (missão §9/§11).
struct RendererCapabilities {
    std::string backendName{};  ///< "Vulkan", "OpenGL ES", "Fake (TESTES)"
    std::string apiVersion{};   ///< "1.4.309", "3.2", ...
    DeviceInfo device{};
    /// Renderização por software (lavapipe/llvmpipe/SwiftShader) — verdade
    /// honesta: suporte real de SOFTWARE não é suporte de hardware (§47).
    bool softwareRendering{false};
    std::uint32_t maxTextureSize{0};
    std::uint32_t maxVertexAttributes{0};
    std::uint32_t maxColorAttachments{0};
    std::uint32_t maxUniformBufferSize{0};
    bool instancing{false};
    bool compute{false};
    bool multisample{false};
    bool wireframe{false};
    /// Formatos com suporte VERIFICADO no device.
    std::vector<Format> supportedFormats{};
    /// Surface/swapchain presentáveis (missão §13: distinto de "device ok").
    bool presentation{false};
    ValidationState validationState{ValidationState::DisabledByConfiguration};
};

// =============================================================================
// Frame (missão §12)
// =============================================================================

struct Viewport {
    float x{0.f};
    float y{0.f};
    float width{0.f};
    float height{0.f};
    float minDepth{0.f};
    float maxDepth{1.f};
};

struct ClearDesc {
    std::array<float, 4> color{0.f, 0.f, 0.f, 1.f};
    bool clearColor{true};
    bool clearDepth{false};
    float depthValue{1.f};
};

/// Estados esperados do loop de apresentação — NÃO são erros (missão §12):
/// fazem parte do protocolo e o chamador decide o que fazer.
enum class FrameAcquireStatus : std::uint8_t {
    Renderable,  ///< comandos podem ser gravados neste frame
    OutOfDate,   ///< surface reconfigurada/recriada; re-tentar beginFrame
    Minimized,   ///< nada a renderizar neste tick
};

// =============================================================================
// Configuração
// =============================================================================

struct SurfaceDesc {
    NativeWindowHandle window{};
    std::uint32_t width{0};
    std::uint32_t height{0};
    /// Formato PREFERIDO — o backend negocia e reporta o real.
    Format preferredFormat{Format::B8G8R8A8Srgb};

    [[nodiscard]] bool isValid() const noexcept {
        return window.isValid() && width > 0 && height > 0;
    }
};

struct RendererConfig {
    BackendType backend{BackendType::Auto};
    /// Desejo do chamador; a realidade está em
    /// RendererCapabilities::validationState (missão §18).
    bool enableValidation{true};
    /// Fallback APENAS quando explicitamente configurado (missão §10).
    /// Nunca silencioso: cada tentativa é logada.
    bool allowFallback{false};
    /// Surface de apresentação. AUSENTE → modo device-only/headless
    /// (missão §13): recursos e capabilities funcionam; frames exigem
    /// surface e retornam erro preciso.
    SurfaceDesc surface{};
    std::string_view applicationName{"eng"};
    /// Dica; backends ajustam/clampam ao seu limite real.
    std::uint32_t framesInFlight{2};
};

} // namespace eng::rhi
