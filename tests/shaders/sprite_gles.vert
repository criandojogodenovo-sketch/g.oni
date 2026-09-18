#version 300 es
// Sprite texturizado (GLES) — vértice: pos vec4 + cor vec4 + uv vec2.
// Espelho do tests/shaders/sprite_vk.vert (paridade, missão §40).
// Nota ES 3.00: inter-stage (out/in) vincula por NOME (lição FASE 6).
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;
out vec4 vColor;
out vec2 vUv;
void main() {
    vColor = inColor;
    vUv = inUv;
    gl_Position = vec4(inPosition.xy, 0.0, 1.0);
}
