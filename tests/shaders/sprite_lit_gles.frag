#version 300 es
// Sprite LIT (GLES) — fragmento: textura * cor * (ambiente + luzes 2D).
// Iluminação 2D FORWARD por-pixel (mobile-friendly: sem render targets):
// o bloco PerFrame (std140) chega via UBO do frame (Frame::setUniformData).
// Espelho do tests/shaders/sprite_lit_vk.frag (paridade, missão §40).
precision mediump float;
in vec4 vColor;
in vec2 vUv;
in vec2 vWorld;
layout(location = 0) out vec4 outColor;
uniform sampler2D uTexture;
layout(std140) uniform PerFrame {
    vec4 uAmbient;     // rgb * intensidade (a)
    vec4 uLightA[8];   // por luz: x, y, raio, intensidade (mundo)
    vec4 uLightB[8];   // por luz: r, g, b, falloff (expoente)
    vec4 uLightMeta;   // x = count
};
void main() {
    vec4 base = vColor * texture(uTexture, vUv);
    vec3 lighting = uAmbient.rgb * uAmbient.a;
    int count = int(uLightMeta.x + 0.5);
    for (int i = 0; i < 8; ++i) {
        if (i >= count) { break; }
        vec4 a = uLightA[i];
        vec4 b = uLightB[i];
        vec2 delta = vWorld - a.xy;
        float dist = length(delta);
        float radius = max(a.z, 0.0001);
        if (dist < radius) {
            float atten = clamp(1.0 - dist / radius, 0.0, 1.0);
            atten = pow(atten, max(b.w, 0.25));
            lighting += b.rgb * (a.w * atten);
        }
    }
    outColor = vec4(base.rgb * lighting, base.a);
}
