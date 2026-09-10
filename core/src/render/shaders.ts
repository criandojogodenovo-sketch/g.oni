/**
 * G.oni Llumni — Fontes GLSL (WebGL2 / GLSL ES 3.00)
 *
 * Pipeline:
 *  1. Sky (gradiente procedural + sol)        — triângulo fullscreen
 *  2. PBR (Cook-Torrance GGX, metal-roughness)— geometria opaca
 *  3. Shadow map (profundidade)               — luz direcional, PCF 3x3
 *  4. Grid infinito                           — triângulo fullscreen com gl_FragDepth
 *  5. Linhas (gizmos/colisores)               — lote dinâmico
 *  6. Cor sólida (bounding boxes, eixos)
 *  7. Pós-processamento: bright → blur gaussiano → composição (ACES + vinheta + bloom)
 */

// ---------- Céu procedural ----------
export const SKY_VS = `#version 300 es
precision highp float;
out vec2 vNDC;
void main() {
  vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  vNDC = pos * 2.0 - 1.0;
  gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}`;

export const SKY_FS = `#version 300 es
precision highp float;
in vec2 vNDC;
uniform mat4 uInvViewProj;
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uSunDir;   // direção DA luz (aponta para a cena)
uniform vec3 uSunColor;
out vec4 fragColor;
void main() {
  vec4 a = uInvViewProj * vec4(vNDC, -1.0, 1.0);
  vec4 b = uInvViewProj * vec4(vNDC, 1.0, 1.0);
  vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
  float h = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
  vec3 col = mix(uHorizon, uZenith, pow(h, 0.75));
  float sd = max(dot(dir, -uSunDir), 0.0);
  col += uSunColor * pow(sd, 1200.0) * 4.0;   // disco solar
  col += uSunColor * pow(sd, 24.0) * 0.25;    // halo
  fragColor = vec4(col, 1.0);
}`;

// ---------- Sombas (mapa de profundidade) ----------
export const SHADOW_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uLightVP;
void main() { gl_Position = uLightVP * uModel * vec4(aPos, 1.0); }`;

export const SHADOW_FS = `#version 300 es
precision highp float;
void main() {}`;

// ---------- PBR (Cook-Torrance GGX, workflow metal-roughness) ----------
export const PBR_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uProj;
uniform mat4 uView;
uniform mat4 uModel;
uniform mat3 uNormalMat;
uniform mat4 uLightVP;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vShadowCoord;
void main() {
  vec4 world = uModel * vec4(aPos, 1.0);
  vWorldPos = world.xyz;
  vNormal = normalize(uNormalMat * aNormal);
  vUV = aUV;
  vShadowCoord = uLightVP * world;
  gl_Position = uProj * uView * world;
}`;

export const PBR_FS = `#version 300 es
precision highp float;
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vShadowCoord;

uniform vec3 uCameraPos;
uniform vec3 uAlbedoColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissive;
uniform float uEmissiveIntensity;
uniform float uOpacity;
uniform float uAlphaCutoff;
uniform sampler2D uAlbedoMap;
uniform sampler2D uNormalMap;
uniform bool uHasAlbedoMap;
uniform bool uHasNormalMap;

uniform vec3 uDirLightDir;
uniform vec3 uDirLightColor;
uniform float uDirLightIntensity;

uniform int uNumPointLights;
uniform vec3 uPointPos[4];
uniform vec3 uPointColor[4];
uniform float uPointIntensity[4];
uniform float uPointRange[4];

uniform int uNumSpotLights;
uniform vec3 uSpotPos[2];
uniform vec3 uSpotDir[2];
uniform vec3 uSpotColor[2];
uniform float uSpotIntensity[2];
uniform float uSpotRange[2];
uniform float uSpotCosAngle[2];

uniform vec3 uAmbientSky;
uniform vec3 uAmbientGround;
uniform float uAmbientIntensity;

uniform sampler2D uShadowMap;
uniform float uShadowStrength;
uniform bool uShadowsEnabled;

out vec4 fragColor;

const float PI = 3.14159265359;

float distributionGGX(float NdotH, float a) {
  float a2 = a * a;
  float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d);
}

float geometrySmith(float NdotV, float NdotL, float a) {
  float k = (a + 1.0) * (a + 1.0) / 8.0;
  float gv = NdotV / (NdotV * (1.0 - k) + k);
  float gl = NdotL / (NdotL * (1.0 - k) + k);
  return gv * gl;
}

vec3 fresnelSchlick(float cosT, vec3 F0) {
  return F0 + (1.0 - F0) * pow(clamp(1.0 - cosT, 0.0, 1.0), 5.0);
}

float sampleShadow(vec3 N, vec3 L) {
  vec3 sc = vShadowCoord.xyz / vShadowCoord.w;
  sc = sc * 0.5 + 0.5;
  if (sc.x < 0.01 || sc.x > 0.99 || sc.y < 0.01 || sc.y > 0.99 || sc.z > 1.0) return 1.0;
  float bias = max(0.002 * (1.0 - dot(N, L)), 0.0006);
  float shadow = 0.0;
  vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
  for (int x = -1; x <= 1; x++) {
    for (int y = -1; y <= 1; y++) {
      float d = texture(uShadowMap, sc.xy + vec2(float(x), float(y)) * texel).r;
      shadow += (sc.z - bias > d) ? 0.0 : 1.0;
    }
  }
  shadow /= 9.0;
  return mix(1.0, shadow, uShadowStrength);
}

// Co-tangent frame: normal mapping sem atributos de tangente (Christian Schüler)
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
  vec3 dp1 = dFdx(p);
  vec3 dp2 = dFdy(p);
  vec2 duv1 = dFdx(uv);
  vec2 duv2 = dFdy(uv);
  vec3 dp2perp = cross(dp2, N);
  vec3 dp1perp = cross(N, dp1);
  vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
  float invmax = 1.0 / max(sqrt(dot(T, T) + dot(B, B)), 1e-7);
  return mat3(T * invmax, B * invmax, N);
}

void main() {
  vec3 albedo = uAlbedoColor;
  if (uHasAlbedoMap) albedo *= texture(uAlbedoMap, vUV).rgb;

  if (uAlphaCutoff > 0.0 && uOpacity < uAlphaCutoff) discard;

  float rough = clamp(uRoughness, 0.045, 1.0);
  float a = rough * rough;

  vec3 N = normalize(vNormal);
  if (uHasNormalMap) {
    vec3 mapN = texture(uNormalMap, vUV).xyz * 2.0 - 1.0;
    N = normalize(cotangentFrame(N, vWorldPos, vUV) * mapN);
  }
  vec3 V = normalize(uCameraPos - vWorldPos);
  vec3 F0 = mix(vec3(0.04), albedo, uMetallic);

  vec3 Lo = vec3(0.0);
  float NdotV = max(dot(N, V), 0.001);

  // --- Luz direcional + sombras ---
  vec3 L = normalize(-uDirLightDir);
  float NdotL = max(dot(N, L), 0.0);
  if (NdotL > 0.0 && uDirLightIntensity > 0.0) {
    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = distributionGGX(NdotH, a);
    float G = geometrySmith(NdotV, NdotL, a);
    vec3 spec = (D * G * F) / (4.0 * NdotV * NdotL + 0.001);
    vec3 kd = (1.0 - F) * (1.0 - uMetallic);
    vec3 diff = kd * albedo / PI;
    float sh = uShadowsEnabled ? sampleShadow(N, L) : 1.0;
    Lo += (diff + spec) * uDirLightColor * uDirLightIntensity * NdotL * sh;
  }

  // --- Luzes pontuais ---
  for (int i = 0; i < 4; i++) {
    if (i >= uNumPointLights) break;
    vec3 Lp = uPointPos[i] - vWorldPos;
    float dist = length(Lp);
    if (dist > uPointRange[i]) continue;
    Lp /= max(dist, 1e-5);
    float ndl = max(dot(N, Lp), 0.0);
    if (ndl <= 0.0) continue;
    float atten = 1.0 / (1.0 + pow(dist / max(uPointRange[i], 1e-4), 2.0));
    vec3 H = normalize(V + Lp);
    float NdotH = max(dot(N, H), 0.0);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = distributionGGX(NdotH, a);
    float G = geometrySmith(NdotV, ndl, a);
    vec3 spec = (D * G * F) / (4.0 * NdotV * ndl + 0.001);
    vec3 kd = (1.0 - F) * (1.0 - uMetallic);
    vec3 diff = kd * albedo / PI;
    Lo += (diff + spec) * uPointColor[i] * uPointIntensity[i] * ndl * atten;
  }

  // --- Spotlights ---
  for (int i = 0; i < 2; i++) {
    if (i >= uNumSpotLights) break;
    vec3 Ls = uSpotPos[i] - vWorldPos;
    float dist = length(Ls);
    if (dist > uSpotRange[i]) continue;
    Ls /= max(dist, 1e-5);
    float cd = dot(Ls, -normalize(uSpotDir[i]));
    if (cd < uSpotCosAngle[i]) continue;
    float spot = (cd - uSpotCosAngle[i]) / (1.0 - uSpotCosAngle[i]);
    spot = spot * spot;
    float ndl = max(dot(N, Ls), 0.0);
    if (ndl <= 0.0) continue;
    float atten = 1.0 / (1.0 + pow(dist / max(uSpotRange[i], 1e-4), 2.0));
    vec3 H = normalize(V + Ls);
    float NdotH = max(dot(N, H), 0.0);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = distributionGGX(NdotH, a);
    float G = geometrySmith(NdotV, ndl, a);
    vec3 spec = (D * G * F) / (4.0 * NdotV * ndl + 0.001);
    vec3 kd = (1.0 - F) * (1.0 - uMetallic);
    vec3 diff = kd * albedo / PI;
    Lo += (diff + spec) * uSpotColor[i] * uSpotIntensity[i] * ndl * atten * spot;
  }

  // --- Ambiente hemisférico ---
  vec3 amb = mix(uAmbientGround, uAmbientSky, N.y * 0.5 + 0.5) * uAmbientIntensity;
  vec3 color = Lo + amb * albedo * (1.0 - uMetallic * 0.85);
  color += uEmissive * uEmissiveIntensity;

  fragColor = vec4(color, uOpacity);
}`;

// ---------- Grid infinito ----------
export const GRID_VS = SKY_VS;

export const GRID_FS = `#version 300 es
precision highp float;
in vec2 vNDC;
uniform mat4 uInvViewProj;
uniform mat4 uViewProj;
uniform vec3 uCamPos;
uniform float uCellSize;
uniform float uFadeDist;
out vec4 fragColor;

float gridFactor(vec2 p, float cell) {
  vec2 g = abs(fract(p / cell - 0.5) - 0.5) / max(fwidth(p), vec2(1e-6));
  float l = min(min(g.x, g.y), 1.0);
  return 1.0 - l;
}

void main() {
  vec4 a = uInvViewProj * vec4(vNDC, -1.0, 1.0);
  vec4 b = uInvViewProj * vec4(vNDC, 1.0, 1.0);
  vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
  if (abs(dir.y) < 1e-5) discard;
  float t = -uCamPos.y / dir.y;
  if (t <= 0.0) discard;
  vec3 wp = uCamPos + dir * t;

  vec4 clip = uViewProj * vec4(wp, 1.0);
  gl_FragDepth = clamp((clip.z / clip.w) * 0.5 + 0.5, 0.0, 1.0);

  float dist = length(wp.xz - uCamPos.xz);
  float fade = 1.0 - smoothstep(uFadeDist * 0.35, uFadeDist, dist);

  float g1 = gridFactor(wp.xz, uCellSize) * 0.32;
  float g2 = gridFactor(wp.xz, uCellSize * 10.0) * 0.55;
  float line = max(g1, g2);

  // Eixos X (vermelho) e Z (azul)
  float ax = 1.0 - min(abs(wp.z) / max(fwidth(wp.z) * 1.2, 1e-6), 1.0);
  float az = 1.0 - min(abs(wp.x) / max(fwidth(wp.x) * 1.2, 1e-6), 1.0);

  vec3 col = vec3(0.55, 0.58, 0.64);
  col = mix(col, vec3(0.95, 0.35, 0.4), ax * 0.85);
  col = mix(col, vec3(0.35, 0.55, 0.95), az * 0.85);

  float alpha = line * fade;
  if (alpha < 0.008) discard;
  fragColor = vec4(col, alpha);
}`;

// ---------- Linhas (gizmos, colisores, outline de seleção) ----------
export const LINE_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uProj;
uniform mat4 uView;
out vec3 vColor;
void main() {
  vColor = aColor;
  gl_Position = uProj * uView * vec4(aPos, 1.0);
}`;

export const LINE_FS = `#version 300 es
precision highp float;
in vec3 vColor;
out vec4 fragColor;
void main() { fragColor = vec4(vColor, 1.0); }`;

// ---------- Cor sólida (setas de gizmo, bounding box) ----------
export const FLAT_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
uniform mat4 uProj;
uniform mat4 uView;
uniform mat4 uModel;
void main() { gl_Position = uProj * uView * uModel * vec4(aPos, 1.0); }`;

export const FLAT_FS = `#version 300 es
precision highp float;
uniform vec3 uColor;
out vec4 fragColor;
void main() { fragColor = vec4(uColor, 1.0); }`;

// ---------- Pós-processamento ----------
export const POST_VS = `#version 300 es
precision highp float;
out vec2 vUV;
void main() {
  vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  vUV = pos;
  gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}`;

export const BRIGHT_FS = `#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uScene;
uniform float uThreshold;
out vec4 fragColor;
void main() {
  vec3 c = texture(uScene, vUV).rgb;
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float k = max(l - uThreshold, 0.0) / max(l, 1e-4);
  fragColor = vec4(c * k, 1.0);
}`;

export const BLUR_FS = `#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uDir; // (1/largura, 0) ou (0, 1/altura)
out vec4 fragColor;
void main() {
  float w[5];
  w[0] = 0.227027; w[1] = 0.194594; w[2] = 0.121621; w[3] = 0.054054; w[4] = 0.016216;
  vec3 c = texture(uTex, vUV).rgb * w[0];
  for (int i = 1; i < 5; i++) {
    vec2 off = uDir * float(i);
    c += texture(uTex, vUV + off).rgb * w[i];
    c += texture(uTex, vUV - off).rgb * w[i];
  }
  fragColor = vec4(c, 1.0);
}`;

export const COMPOSITE_FS = `#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uExposure;
uniform float uVignette;
out vec4 fragColor;

vec3 aces(vec3 x) {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
  vec3 c = texture(uScene, vUV).rgb;
  c += texture(uBloom, vUV).rgb * uBloomStrength;
  c *= uExposure;
  c = aces(c);
  vec2 d = vUV - 0.5;
  c *= 1.0 - uVignette * dot(d, d) * 2.2;
  c = pow(c, vec3(1.0 / 2.2));
  fragColor = vec4(c, 1.0);
}`;

export const BLIT_FS = `#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uScene;
out vec4 fragColor;
void main() { fragColor = texture(uScene, vUV); }`;
