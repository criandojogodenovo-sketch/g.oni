#pragma once

/// eng::assets::AssetType — semântica do asset (FASE 3; ADR-029).
#include <cstdint>
#include <string_view>

#include "eng/core/Result.hpp"

namespace eng::assets {

enum class AssetType : std::uint32_t {
    Unknown = 0,

    // --- implementados na FASE 3 (com loader JSON) ------------------------
    Scene = 1,   ///< cena serializada (interpretada por eng::scene)
    Prefab = 2,  ///< prefab JSON (estrutura interpretada pelo consumidor)
    Json = 3,    ///< JSON genérico (settings etc.)

    // --- RESERVADOS (missão §2.12): declarados, SEM loader nesta fase.
    // Valores com folga para nunca colidir com os implementados.
    Texture = 100,
    Mesh = 101,
    Material = 102,
    Shader = 103,
    Audio = 104,
    Script = 105,
};

/// Nome estável ("Scene", "Texture", ...) — chave de serialização.
[[nodiscard]] std::string_view assetTypeName(AssetType type) noexcept;

/// Parse do nome estável (inverse de assetTypeName). Desconhecido → erro.
[[nodiscard]] eng::core::Result<AssetType> assetTypeFromName(
    std::string_view name);

} // namespace eng::assets
