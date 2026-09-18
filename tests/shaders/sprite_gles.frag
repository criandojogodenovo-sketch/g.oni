#version 300 es
// Sprite texturizado (GLES) — fragmento: textura * cor do vértice.
// Espelho do tests/shaders/sprite_vk.frag (paridade, missão §40).
// uniform sampler2D: unidade 0 (padrão) — frameBindTexture(slot 0).
precision mediump float;
in vec4 vColor;
in vec2 vUv;
layout(location = 0) out vec4 outColor;
uniform sampler2D uTexture;
void main() {
    outColor = vColor * texture(uTexture, vUv);
}
