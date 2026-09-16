#version 300 es
// Triangle (GLES) — fragmento: repassa a cor interpolada.
// Espelho do tests/shaders/triangle_vk.frag (paridade, missão §40).
precision mediump float;
in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vColor;
}
