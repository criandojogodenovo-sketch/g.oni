#version 450
// Triangle (Vulkan) — fragmento: repassa a cor interpolada.
// Espelho do tests/shaders/triangle_gles.frag (paridade FASE 6, missão §40).
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vColor;
}
