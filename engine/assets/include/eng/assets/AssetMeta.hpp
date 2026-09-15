#pragma once

/// eng::assets::AssetMeta — metadados de um asset catalogado (FASE 3; ADR-029).
///
/// - sourcePath é SEMPRE relativo à raiz de assets do projeto (missão §2.12);
///   caminho absoluto é erro de validação no resolve (AssetResolver).
/// - size é informativo (não verificá-lo na FASE 3).
/// - contentHash é campo DECLARADO para dedup/cache futuros — NÃO calculado
///   nesta fase (missão §2.3: "contentHash será metadado, não identidade").
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "eng/assets/AssetId.hpp"
#include "eng/assets/AssetType.hpp"
#include "eng/fs/Path.hpp"

namespace eng::assets {

struct AssetMeta {
    AssetId id;
    AssetType type = AssetType::Unknown;
    eng::fs::Path sourcePath;

    /// Tamanho do arquivo fonte em bytes (informativo; opcional).
    std::optional<std::uint64_t> size;

    /// Hash de conteúdo de 128 bits — reservado; NUNCA preenchido na FASE 3
    /// (identidade é o AssetId; dedup por conteúdo é fase futura).
    std::optional<std::array<std::byte, 16>> contentHash;

    [[nodiscard]] bool operator==(const AssetMeta&) const = default;
};

} // namespace eng::assets
