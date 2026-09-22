#pragma once

/// eng::editor::OverlayMath — geometria canônica dos OVERLAYS do editor
/// (P4.3 — N3/N4; ver docs/p4-editor-ux.md).
///
/// CAUSA RAIZ corrigida: toda a geometria (gizmos, bounds, grid,
/// contornos de collider, partículas, marcador de luz, SPRITES) era
/// montada em CLIP SPACE com a rotação aplicada dentro do clip — mas os
/// eixos do clip têm escalas de pixel DIFERENTES (X: w/2 px por unidade,
/// Y: h/2 px). Num portrait 720×1600, um quad rodado 90° ficava 2,22×
/// mais longo no eixo Y ("rotação deforma sprites") e a espessura dos
/// segmentos variava com a direção (anel de rotação "elíptico").
///
/// REGRA ÚNICA (N4 — um só helper canónico): a geometria de QUALQUER
/// overlay nasce em PX DE TELA — centros, meia-extensões, rotação e
/// espessuras — e só vira clip no ÚLTIMO passo, por eixo (x/w·2−1,
/// 1−y/h·2). Rotação em px é isotrópica: quadrados são quadrados e
/// círculos são círculos em qualquer aspect.
///
/// Conversões mundo→px: Viewport::worldToScreenX/Y (a MESMA fonte do
/// hit-test e do gizmo — câmera em foco incluída). Nada aqui conhece
/// mundo: só px ↔ clip.

#include <cmath>

namespace eng::editor {

/// Conversor px→clip da surface corrente (w/h em px — viewport.screenWidth/
/// screenHeight). Um único caminho para TODO o overlay (renderer + testes).
struct OverlayMapper {
    float w{1.f};
    float h{1.f};

    /// Ponto de tela (px) → clip X.
    [[nodiscard]] float toClipX(float sx) const noexcept
    {
        return (sx / w) * 2.f - 1.f;
    }
    /// Ponto de tela (px) → clip Y (origem no TOPO — convenção Android).
    [[nodiscard]] float toClipY(float sy) const noexcept
    {
        return 1.f - (sy / h) * 2.f;
    }
};

/// Canto de quad em PX (saída de quadCornersPx / segmentCornersPx).
struct PxCorner {
    float x{0.f};
    float y{0.f};
};

/// Quatro cantos em PX de um quad centrado (cx,cy), meia-extensões
/// (halfW,halfH) e rotação `rotation` (radianos, mesmo sentido do clip:
/// px Y cresce para baixo — cos/sin direto). ROTAÇÃO EM PX: cantos de um
/// quadrado permanecem equidistantes em px para qualquer rotação.
inline void quadCornersPx(float cx, float cy, float halfW, float halfH,
                          float rotation, PxCorner out[4]) noexcept
{
    const float cosR = std::cos(rotation);
    const float sinR = std::sin(rotation);
    const float lx[4] = {-halfW, halfW, halfW, -halfW};
    const float ly[4] = {-halfH, -halfH, halfH, halfH};
    for (int i = 0; i < 4; ++i) {
        out[i].x = cx + lx[i] * cosR - ly[i] * sinR;
        out[i].y = cy + lx[i] * sinR + ly[i] * cosR;
    }
}

/// Quatro cantos em PX do quad ESPESSO de um segmento a→b (px) com
/// meia-espessura `halfThick` px — perpendicular calculada em px
/// (isotrópico): a espessura VISÍVEL é a mesma em qualquer direção.
inline void segmentCornersPx(float ax, float ay, float bx, float by,
                             float halfThick, PxCorner out[4]) noexcept
{
    const float dx = bx - ax;
    const float dy = by - ay;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-6f) {
        // Segmento degenerado → quad nulo (o renderer descarta).
        for (int i = 0; i < 4; ++i) {
            out[i].x = ax;
            out[i].y = ay;
        }
        return;
    }
    // Perpendicular unitária (px) × meia-espessura.
    const float nx = -dy / length * halfThick;
    const float ny = dx / length * halfThick;
    // Ordem CCW compatível com quadCornersPx: (-,-),(+,-),(+,+),(-,+)
    // no frame (comprimento × espessura) do segmento.
    out[0] = {ax - nx, ay - ny};  // A − perp
    out[1] = {bx - nx, by - ny};  // B − perp
    out[2] = {bx + nx, by + ny};  // B + perp
    out[3] = {ax + nx, ay + ny};  // A + perp
}

} // namespace eng::editor
