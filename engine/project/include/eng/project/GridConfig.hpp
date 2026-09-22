#pragma once

/// eng::project::GridConfig — grade de ENGINE do viewport (P4.6 Bloco 5/L2,
/// padrão Godot/Unity/3ds Max): passo em UNIDADES DE MUNDO, linhas
/// minor/major ("Primary Line Every" a cada N), LOD adaptativo ao zoom
/// (anti-moiré) e eixos da origem coloridos.
///
/// Mora no project.goni.json (chave aditiva "grid" — ausente = default,
/// mesma política de collisionLayers) porque o AUTOR configura o passo do
/// SEU projeto (1u por tile, 0.5u por metro de pixel-art…).

#include <algorithm>
#include <cmath>

namespace eng::project {

/// Grade desligada? Renderizador desenha só o clear + entidades.
struct GridConfig {
    bool visible{true};
    float cell{1.f};   ///< passo MINOR em unidades de mundo (> 0)
    int majorEvery{8}; ///< major a cada N minors (>= 2 — padrão Godot 8)
    /// Cores das linhas (0..1 — minor sutil, major mais clara).
    float minorR{0.20f}, minorG{0.21f}, minorB{0.24f};
    float majorR{0.32f}, majorG{0.34f}, majorB{0.40f};

    [[nodiscard]] bool operator==(const GridConfig&) const = default;
};

/// --- LOD adaptativo (função PURA — testável sem GPU) -----------------------
///
/// Espaçamentos em px de tela por nível de zoom:
///   - majors NUNCA mais densas que kGridMajorMinPx (zoom-out: o major do
///     nível atual "vira" o minor do nível seguinte — subdivisão emerge
///     ao aproximar, exatamente como Godot);
///   - minors FADEAM entre kGridMinorFadeStartPx e kGridMinorFadeEndPx
///     (anti-moiré: abaixo do fim do fade elas NÃO desenham).
inline constexpr float kGridMajorMinPx = 10.f;
inline constexpr float kGridMinorFadeStartPx = 16.f;
inline constexpr float kGridMinorFadeEndPx = 6.f;

struct GridLod {
    float minorStep{1.f}; ///< espaçamento das linhas minor (mundo)
    float majorStep{8.f}; ///< espaçamento das linhas major (mundo)
    float minorAlpha{1.f}; ///< 0 = minors escondidas; 1 = plenas
};

[[nodiscard]] inline GridLod computeGridLod(const GridConfig& grid,
                                            float zoom) noexcept
{
    GridLod lod;
    lod.minorStep = grid.cell > 0.f && std::isfinite(grid.cell)
                        ? grid.cell
                        : 1.f;
    const int every = grid.majorEvery >= 2 ? grid.majorEvery : 8;
    lod.majorStep = lod.minorStep * static_cast<float>(every);
    // Zoom-out: enquanto o MAJOR do nível for denso demais, sobe de nível
    // (o major vira o minor do nível seguinte). Teto defensivo (float).
    for (int i = 0; i < 16 && lod.majorStep * zoom < kGridMajorMinPx; ++i) {
        lod.minorStep = lod.majorStep;
        lod.majorStep = lod.minorStep * static_cast<float>(every);
    }
    // Fade das minors do nível atual (anti-moiré).
    const float pxMinor = lod.minorStep * zoom;
    lod.minorAlpha = std::clamp(
        (pxMinor - kGridMinorFadeEndPx) /
            (kGridMinorFadeStartPx - kGridMinorFadeEndPx),
        0.f, 1.f);
    return lod;
}

} // namespace eng::project
