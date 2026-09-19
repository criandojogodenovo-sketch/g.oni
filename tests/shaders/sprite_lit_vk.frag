#version 450
// Sprite LIT (Vulkan) — fragmento: textura * cor * (ambiente + luzes 2D).
// Iluminação 2D FORWARD por-pixel (mobile-friendly: sem render targets).
// set 0 / binding 0 = combined image sampler (layout do backend).
// set 1 / binding 0 = UBO dinâmico do frame (bloco PerFrame, std140).
layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(std140, set = 1, binding = 0) uniform PerFrame {
    vec4 uAmbient;     // rgb * intensidade (a)
    vec4 uLightA[8];   // por luz: x, y, raio, intensidade (mundo)
    vec4 uLightB[8];   // por luz: r, g, b, falloff (expoente)
    vec4 uLightMeta;   // x = count
};
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec2 vWorld;
layout(location = 0) out vec4 outColor;
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
