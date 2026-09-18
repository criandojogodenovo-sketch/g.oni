#pragma once

/// eng::editor::SpriteData — componente visual de sprite com TEXTURA REAL
/// (evolução P0-3: o fim do "retângulo colorido").
///
/// Modelo (missão 2D IMAGE/SPRITE WORKFLOW):
/// - `textureAsset`: NOME do asset de textura no projeto (categoria
///   "textures" do AssetBrowser — o TextureCache do host resolve para o
///   AssetId do registry e sobe p/ GPU);
/// - região UV (u0,v0)-(u1,v1): sprite sheet suportado por fatiamento;
/// - pivot: âncora local [0..1]² (centro 0.5,0.5 default);
/// - flip X/Y, tint RGB multiplicativo + opacity [0..1];
/// - sort/z: ordem de desenho (maior = frente);
/// - pixelsPerUnit: escala mundo do sprite (1 texel = N unidades).
///
/// Registrado no catálogo ÚNICO do SceneSerializer (ADR-043): aparece no
/// Inspector, persiste em cena e clona no Play. Campos FLAT (sem arrays —
/// reflexão/inspector/serializer operam por caminho simples).

#include <string>

#include "eng/reflect/Reflect.hpp"

namespace eng::editor {

struct SpriteData {
    /// Nome do asset de textura (vazio = sem textura — quad de cor).
    std::string textureAsset{};

    // --- região (UV) -------------------------------------------------------
    float u0{0.f};
    float v0{0.f};
    float u1{1.f};
    float v1{1.f};

    // --- pivot/flip ---------------------------------------------------------
    float pivotX{0.5f};
    float pivotY{0.5f};
    bool flipX{false};
    bool flipY{false};

    // --- aparência -----------------------------------------------------------
    float tintR{1.f};
    float tintG{1.f};
    float tintB{1.f};
    float opacity{1.f};

    // --- ordenação ------------------------------------------------------------
    float sort{0.f};
    float pixelsPerUnit{1.f};
};

}  // namespace eng::editor

/// Reflexão (ADR-043: campos por caminho — Inspector/serializer).
/// Hints de edição (P0-6, ADR-052): textureAsset → picker de texturas;
/// tintR/G/B → UM editor de cor (grupo 0) — serialização INALTERADA.
/// clang-format off
ENG_REFLECT_BEGIN(eng::editor::SpriteData)
    ENG_REFLECT_FIELD_HINT(textureAsset, "texture")
    ENG_REFLECT_FIELD(u0)
    ENG_REFLECT_FIELD(v0)
    ENG_REFLECT_FIELD(u1)
    ENG_REFLECT_FIELD(v1)
    ENG_REFLECT_FIELD(pivotX)
    ENG_REFLECT_FIELD(pivotY)
    ENG_REFLECT_FIELD(flipX)
    ENG_REFLECT_FIELD(flipY)
    ENG_REFLECT_FIELD_HINT(tintR, "color:0:r")
    ENG_REFLECT_FIELD_HINT(tintG, "color:0:g")
    ENG_REFLECT_FIELD_HINT(tintB, "color:0:b")
    ENG_REFLECT_FIELD(opacity)
    ENG_REFLECT_FIELD(sort)
    ENG_REFLECT_FIELD(pixelsPerUnit)
ENG_REFLECT_END()
/// clang-format on
