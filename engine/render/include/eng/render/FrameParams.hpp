#pragma once

/// eng::render::FrameParams — dados de uniform do frame 2D (bloco std140
/// "PerFrame" compartilhado pelos shaders de sprite — P3, §2/§5).
///
/// O layout do bloco é FIXO e espelhado nos shaders (tests/shaders/
/// sprite_lit_{vk,gles}.frag):
///
///   layout(std140) uniform PerFrame {
///       vec4 uAmbient;     // offset   0 — rgb * intensidade (a)
///       vec4 uLightA[8];   // offset  16 — por luz: x, y, raio, intensidade
///       vec4 uLightB[8];   // offset 144 — por luz: r, g, b, falloff
///       vec4 uLightMeta;   // offset 272 — x = count
///   };                    // TOTAL: 288 bytes
///
/// A struct C++ `FrameUniforms` USA O MESMO LAYOUT (std140: array de vec4
/// tem stride 16 garantido) — o pack é um memcpy direto, sem conversão.
/// Luzes vivem em espaço de MUNDO (posição do fragmento interpolada por
/// vértice — attribute location 3 dos sprites LIT).

#include <array>
#include <cstddef>
#include <cstdint>

namespace eng::render {

/// Máximo de luzes por conjunto de uniform (banco do bloco).
inline constexpr std::uint32_t kMaxLights = 8;

/// Bloco std140 "PerFrame" — layout BINÁRIO FIXO espelhado nos shaders.
/// (Trivially-copyable; o renderer envia `sizeof(FrameUniforms)` bytes.)
struct FrameUniforms {
    /// Ambiente: rgb * intensidade (a). Default (1,1,1)*1 = sem luzes o
    /// sprite sai EXATAMENTE como no pipeline unlit (decisão honesta: luz
    /// ADICIONA sobre o ambiente — nunca escurece o look sem luz).
    float ambient[4]{1.f, 1.f, 1.f, 1.f};
    /// Por luz [i]: x, y (mundo), raio (unidades), intensidade.
    float lightA[kMaxLights][4]{};
    /// Por luz [i]: r, g, b, falloff (expoente da atenuação).
    float lightB[kMaxLights][4]{};
    /// Meta: x = count de luzes válidas; resto padding std140.
    float lightMeta[4]{0.f, 0.f, 0.f, 0.f};

    /// Empacota um conjunto amigável no layout do bloco.
    void setLight(std::uint32_t index, float worldX, float worldY,
                  float radius, float intensity, float r, float g,
                  float b, float falloff) noexcept
    {
        if (index >= kMaxLights) {
            return;
        }
        lightA[index][0] = worldX;
        lightA[index][1] = worldY;
        lightA[index][2] = radius;
        lightA[index][3] = intensity;
        lightB[index][0] = r;
        lightB[index][1] = g;
        lightB[index][2] = b;
        lightB[index][3] = falloff;
    }

    void setLightCount(std::uint32_t count) noexcept
    {
        lightMeta[0] = static_cast<float>(count > kMaxLights ? kMaxLights
                                                             : count);
    }

    /// Contagem embutida no bloco (arredondamento defensivo do shader).
    [[nodiscard]] std::uint32_t lightCount() const noexcept
    {
        return static_cast<std::uint32_t>(lightMeta[0] + 0.5f);
    }

    [[nodiscard]] bool operator==(const FrameUniforms&) const = default;
};

/// Tamanho do bloco enviado ao RHI (std140 — 18 vec4).
inline constexpr std::size_t kFrameUniformSize = sizeof(FrameUniforms);
static_assert(kFrameUniformSize == 288,
              "PerFrame std140: layout fixo espelhado nos shaders");

/// Nome do bloco nos shaders (contrato do ShaderDesc::uniformBlockName).
inline constexpr char kFrameUniformBlockName[] = "PerFrame";

}  // namespace eng::render
