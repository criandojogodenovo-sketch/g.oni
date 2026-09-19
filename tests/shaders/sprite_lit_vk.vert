#version 450
// Sprite LIT (Vulkan) — vértice: pos clip vec4 + cor vec4 + uv vec2 + MUNDO vec2.
// Compilado para SPIR-V (vulkan1.1) — ver sprite_lit_vk_vert_spirv.hpp.
// O fragment de iluminação precisa da posição MUNDIAL do fragmento (a luz
// vive em espaço de mundo); o batcher CPU envia clip E mundo por vértice.
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec2 inWorld;
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec2 vWorld;
void main() {
    vColor = inColor;
    vUv = inUv;
    vWorld = inWorld;
    gl_Position = vec4(inPosition.xy, 0.0, 1.0);
}
