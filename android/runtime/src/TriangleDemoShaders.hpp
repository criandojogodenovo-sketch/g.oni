#pragma once

/// TriangleDemo — shaders embutidos do runtime de demonstração (FASE 7,
/// missão §XVI/XVII).
///
/// PROCEDÊNCIA: cópias EXATAS dos fixtures validados das FASES 5/6
/// (tests/shaders/triangle_*.vert/.frag e os .spv gerados por
/// glslangValidator 15.1 --target-env vulkan1.1 -V, validados com
/// spirv-val — regeneração documentada no README local). O runtime embute
/// porque o APK de demonstração não carrega arquivos (missão §XVII:
/// teste mínimo, não um sistema de materiais).
///
/// - GLSL ES 300 -> backend OpenGL ES (compila em runtime, info log real)
/// - SPIR-V (246 words vertex / 94 words fragment) -> backend Vulkan

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace eng::android {

/// GLSL ES 300 — vertex (espelho de tests/shaders/triangle_gles.vert).
inline constexpr std::string_view kTriangleVertexGlsl = R"GLSL(#version 300 es
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;
out vec4 vColor;
void main() {
    vColor = inColor;
    gl_Position = vec4(inPosition.xy, 0.0, 1.0);
}
)GLSL";

/// GLSL ES 300 — fragment (espelho de tests/shaders/triangle_gles.frag).
inline constexpr std::string_view kTriangleFragmentGlsl = R"GLSL(#version 300 es
precision mediump float;
in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vColor;
}
)GLSL";

/// SPIR-V — vertex (cópia de tests/shaders/triangle_vk_vert_spirv.hpp).
inline constexpr std::array<std::uint32_t, 246> kTriangleVertexSpirv =
{
    0x07230203u, 0x00010300u, 0x0008000bu, 0x0000001fu, 0x00000000u, 0x00020011u,
    0x00000001u, 0x0006000bu, 0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu,
    0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0009000fu, 0x00000000u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x00000009u, 0x0000000bu, 0x00000012u,
    0x00000015u, 0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u, 0x00000004u,
    0x6e69616du, 0x00000000u, 0x00040005u, 0x00000009u, 0x6c6f4376u, 0x0000726fu,
    0x00040005u, 0x0000000bu, 0x6f436e69u, 0x00726f6cu, 0x00060005u, 0x00000010u,
    0x505f6c67u, 0x65567265u, 0x78657472u, 0x00000000u, 0x00060006u, 0x00000010u,
    0x00000000u, 0x505f6c67u, 0x7469736fu, 0x006e6f69u, 0x00070006u, 0x00000010u,
    0x00000001u, 0x505f6c67u, 0x746e696fu, 0x657a6953u, 0x00000000u, 0x00070006u,
    0x00000010u, 0x00000002u, 0x435f6c67u, 0x4470696cu, 0x61747369u, 0x0065636eu,
    0x00070006u, 0x00000010u, 0x00000003u, 0x435f6c67u, 0x446c6c75u, 0x61747369u,
    0x0065636eu, 0x00030005u, 0x00000012u, 0x00000000u, 0x00050005u, 0x00000015u,
    0x6f506e69u, 0x69746973u, 0x00006e6fu, 0x00040047u, 0x00000009u, 0x0000001eu,
    0x00000000u, 0x00040047u, 0x0000000bu, 0x0000001eu, 0x00000001u, 0x00030047u,
    0x00000010u, 0x00000002u, 0x00050048u, 0x00000010u, 0x00000000u, 0x0000000bu,
    0x00000000u, 0x00050048u, 0x00000010u, 0x00000001u, 0x0000000bu, 0x00000001u,
    0x00050048u, 0x00000010u, 0x00000002u, 0x0000000bu, 0x00000003u, 0x00050048u,
    0x00000010u, 0x00000003u, 0x0000000bu, 0x00000004u, 0x00040047u, 0x00000015u,
    0x0000001eu, 0x00000000u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u,
    0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u,
    0x00000006u, 0x00000004u, 0x00040020u, 0x00000008u, 0x00000003u, 0x00000007u,
    0x0004003bu, 0x00000008u, 0x00000009u, 0x00000003u, 0x00040020u, 0x0000000au,
    0x00000001u, 0x00000007u, 0x0004003bu, 0x0000000au, 0x0000000bu, 0x00000001u,
    0x00040015u, 0x0000000du, 0x00000020u, 0x00000000u, 0x0004002bu, 0x0000000du,
    0x0000000eu, 0x00000001u, 0x0004001cu, 0x0000000fu, 0x00000006u, 0x0000000eu,
    0x0006001eu, 0x00000010u, 0x00000007u, 0x00000006u, 0x0000000fu, 0x0000000fu,
    0x00040020u, 0x00000011u, 0x00000003u, 0x00000010u, 0x0004003bu, 0x00000011u,
    0x00000012u, 0x00000003u, 0x00040015u, 0x00000013u, 0x00000020u, 0x00000001u,
    0x0004002bu, 0x00000013u, 0x00000014u, 0x00000000u, 0x0004003bu, 0x0000000au,
    0x00000015u, 0x00000001u, 0x00040017u, 0x00000016u, 0x00000006u, 0x00000002u,
    0x0004002bu, 0x00000006u, 0x00000019u, 0x00000000u, 0x0004002bu, 0x00000006u,
    0x0000001au, 0x3f800000u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u,
    0x00000003u, 0x000200f8u, 0x00000005u, 0x0004003du, 0x00000007u, 0x0000000cu,
    0x0000000bu, 0x0003003eu, 0x00000009u, 0x0000000cu, 0x0004003du, 0x00000007u,
    0x00000017u, 0x00000015u, 0x0007004fu, 0x00000016u, 0x00000018u, 0x00000017u,
    0x00000017u, 0x00000000u, 0x00000001u, 0x00050051u, 0x00000006u, 0x0000001bu,
    0x00000018u, 0x00000000u, 0x00050051u, 0x00000006u, 0x0000001cu, 0x00000018u,
    0x00000001u, 0x00070050u, 0x00000007u, 0x0000001du, 0x0000001bu, 0x0000001cu,
    0x00000019u, 0x0000001au, 0x00050041u, 0x00000008u, 0x0000001eu, 0x00000012u,
    0x00000014u, 0x0003003eu, 0x0000001eu, 0x0000001du, 0x000100fdu, 0x00010038u,};

/// SPIR-V — fragment (cópia de tests/shaders/triangle_vk_frag_spirv.hpp).
inline constexpr std::array<std::uint32_t, 94> kTriangleFragmentSpirv =
{
    0x07230203u, 0x00010300u, 0x0008000bu, 0x0000000du, 0x00000000u, 0x00020011u,
    0x00000001u, 0x0006000bu, 0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu,
    0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0007000fu, 0x00000004u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x00000009u, 0x0000000bu, 0x00030010u,
    0x00000004u, 0x00000007u, 0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x00050005u, 0x00000009u, 0x4374756fu,
    0x726f6c6fu, 0x00000000u, 0x00040005u, 0x0000000bu, 0x6c6f4376u, 0x0000726fu,
    0x00040047u, 0x00000009u, 0x0000001eu, 0x00000000u, 0x00040047u, 0x0000000bu,
    0x0000001eu, 0x00000000u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u,
    0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u,
    0x00000006u, 0x00000004u, 0x00040020u, 0x00000008u, 0x00000003u, 0x00000007u,
    0x0004003bu, 0x00000008u, 0x00000009u, 0x00000003u, 0x00040020u, 0x0000000au,
    0x00000001u, 0x00000007u, 0x0004003bu, 0x0000000au, 0x0000000bu, 0x00000001u,
    0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u,
    0x00000005u, 0x0004003du, 0x00000007u, 0x0000000cu, 0x0000000bu, 0x0003003eu,
    0x00000009u, 0x0000000cu, 0x000100fdu, 0x00010038u,};

inline std::span<const std::byte> kTriangleVertexSpirvBytes() {
    return {reinterpret_cast<const std::byte*>(kTriangleVertexSpirv.data()),
            kTriangleVertexSpirv.size() * sizeof(std::uint32_t)};
}
inline std::span<const std::byte> kTriangleFragmentSpirvBytes() {
    return {reinterpret_cast<const std::byte*>(kTriangleFragmentSpirv.data()),
            kTriangleFragmentSpirv.size() * sizeof(std::uint32_t)};
}

/// Vertex data do triangle (pos vec4 + cor vec4, stride 32) — o MESMO das
/// FASES 5/6 (paridade).
struct TriangleVertices {
    static constexpr float kData[] = {
        -0.75f, -0.75f, 0.f, 1.f, 1.0f, 0.2f, 0.2f, 1.0f,
         0.75f, -0.75f, 0.f, 1.f, 0.2f, 1.0f, 0.2f, 1.0f,
         0.0f,   0.75f, 0.f, 1.f, 0.2f, 0.2f, 1.0f, 1.0f,
    };
    static constexpr std::uint32_t kCount = 3;
    static constexpr std::uint32_t kStride = 32;
};

}  // namespace eng::android
