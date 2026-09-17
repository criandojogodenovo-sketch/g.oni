#pragma once

/// EditorShaders — pipeline pos+cor do VIEWPORT do editor (FASE 8, §8.6).
///
/// PROCEDÊNCIA: cópias EXATAS (byte-a-byte) dos fixtures canônicos das
/// FASES 5/6, os mesmos embutidos no runtime Android (FASE 7):
///   - GLSL ES 300: tests/shaders/triangle_gles.{vert,frag}
///     (inter-stage por NOME — ES 3.00, lição FASE 6);
///   - SPIR-V: tests/shaders/triangle_vk_{vert,frag}_spirv.hpp
///     (glslangValidator 15.1 --target-env vulkan1.1 -V + spirv-val).
/// PROCEDÊNCIA dos dados: cópias EXATAS dos fixtures acima (tests/shaders)
/// — este header é a fonte única no repositório. NOTA (auditoria final
/// 4–10): o script `gen_editor_shaders.py` citado antes NÃO existe no
/// repositório; a referência de regeneração era falsa e foi removida.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace eng::editor {

/// GLSL ES 300 — vertex (espelho de tests/shaders/triangle_gles.vert).
inline constexpr std::string_view kEditorVertexGlsl = R"GLSL(#version 300 es
// Triangle (GLES) — vértice: pos vec4 (xy usados) + cor vec4.
// Espelho do tests/shaders/triangle_vk.vert (paridade, missão §40).
// Nota ES 3.00: locations explícitos são permitidos em vertex INPUTS e
// fragment OUTPUT; inter-stage (out/in) vincula por NOME (3.10+ exigiria
// GL_EXT_separate_shader_objects).
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;
out vec4 vColor;
void main() {
    vColor = inColor;
    gl_Position = vec4(inPosition.xy, 0.0, 1.0);
}
)GLSL";

/// GLSL ES 300 — fragment (espelho de tests/shaders/triangle_gles.frag).
inline constexpr std::string_view kEditorFragmentGlsl = R"GLSL(#version 300 es
// Triangle (GLES) — fragmento: repassa a cor interpolada.
// Espelho do tests/shaders/triangle_vk.frag (paridade, missão §40).
precision mediump float;
in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vColor;
}
)GLSL";

/// SPIR-V — vertex (246 words; cópia de triangle_vk_vert_spirv.hpp).
inline constexpr std::array<std::uint32_t, 246> kEditorVertexSpirv = {
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
    0x00000014u, 0x0003003eu, 0x0000001eu, 0x0000001du, 0x000100fdu, 0x00010038u,
};

/// SPIR-V — fragment (94 words; cópia de triangle_vk_frag_spirv.hpp).
inline constexpr std::array<std::uint32_t, 94> kEditorFragmentSpirv = {
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
    0x00000009u, 0x0000000cu, 0x000100fdu, 0x00010038u,
};

[[nodiscard]] inline std::span<const std::byte> kEditorVertexSpirvBytes() {
    return {reinterpret_cast<const std::byte*>(kEditorVertexSpirv.data()),
            kEditorVertexSpirv.size() * sizeof(std::uint32_t)};
}
[[nodiscard]] inline std::span<const std::byte> kEditorFragmentSpirvBytes() {
    return {reinterpret_cast<const std::byte*>(kEditorFragmentSpirv.data()),
            kEditorFragmentSpirv.size() * sizeof(std::uint32_t)};
}

}  // namespace eng::editor
