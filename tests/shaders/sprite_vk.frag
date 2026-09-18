#version 450
// Sprite texturizado (Vulkan) — fragmento: textura * cor do vértice.
// set 0 / binding 0 = combined image sampler (layout do backend).
layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vColor * texture(uTexture, vUv);
}
