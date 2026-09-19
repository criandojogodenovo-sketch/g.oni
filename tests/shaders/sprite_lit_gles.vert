#version 300 es
// Sprite LIT (GLES) — vértice: pos clip vec4 + cor vec4 + uv vec2 + MUNDO vec2.
// O fragment de iluminação precisa da posição MUNDIAL do fragmento (a luz
// vive em espaço de mundo); o batcher CPU envia clip E mundo por vértice.
// Espelho do tests/shaders/sprite_lit_vk.vert (paridade, missão §40).
// Nota ES 3.00: inter-stage (out/in) vincula por NOME (lição FASE 6).
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec2 inWorld;
out vec4 vColor;
out vec2 vUv;
out vec2 vWorld;
void main() {
    vColor = inColor;
    vUv = inUv;
    vWorld = inWorld;
    gl_Position = vec4(inPosition.xy, 0.0, 1.0);
}
