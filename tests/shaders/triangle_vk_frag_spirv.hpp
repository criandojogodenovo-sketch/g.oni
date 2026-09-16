#pragma once
/// GERADO de triangle_vk.frag.spv — glslangValidator 15.1 (--target-env vulkan1.1 -V),
/// validado com spirv-val. NÃO editar à mão: regenere o .spv e este header
/// juntos (procedimento em tests/shaders/README.md).
#include <array>
#include <cstddef>
#include <cstdint>
namespace eng::rhi::testing {
inline constexpr std::array<std::uint32_t, 94> kTriangleFragmentSpirv = {
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
inline std::span<const std::byte> kTriangleFragmentSpirvBytes() {
    return {reinterpret_cast<const std::byte*>(kTriangleFragmentSpirv.data()),
            kTriangleFragmentSpirv.size() * sizeof(std::uint32_t)};
}
}  // namespace eng::rhi::testing
