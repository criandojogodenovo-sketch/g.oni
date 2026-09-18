#version 450
// Sprite texturizado (Vulkan) — vértice: pos vec4 + cor vec4 + uv vec2.
// Compilado para SPIR-V (vulkan1.1) — ver sprite_vk_{vert,frag}_spirv.hpp.
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;
void main() {
    vColor = inColor;
    vUv = inUv;
    gl_Position = vec4(inPosition.xy, 0.0, 1.0);
}
