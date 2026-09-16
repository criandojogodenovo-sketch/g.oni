#version 450
// Triangle (Vulkan) — vértice: pos vec4 (xy usados) + cor vec4.
// Espelho do tests/shaders/triangle_gles.vert (paridade FASE 6, missão §40).
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 vColor;
void main() {
    vColor = inColor;
    gl_Position = vec4(inPosition.xy, 0.0, 1.0);
}
