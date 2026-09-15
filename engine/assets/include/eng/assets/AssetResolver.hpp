#pragma once

/// eng::assets::AssetResolver — AssetId → bytes do arquivo (FASE 3; ADR-029).
///
/// Caminho de resolução (com a defesa anti-traversal da missão §4.6):
///   1. id consultado no AssetRegistry (ausente → NotFound);
///   2. sourcePath precisa ser RELATIVO (absoluto → InvalidArgument);
///   3. root.join(sourcePath).normalize() precisa permanecer DENTRO de
///      root (fs::Path::isWithin — componente a componente; "a/../../x"
///      normaliza para fora e é rejeitado);
///   4. leitura via FileSystem informado (MemoryFileSystem nos testes).
#include <span>
#include <vector>

#include "eng/assets/AssetId.hpp"
#include "eng/assets/AssetRegistry.hpp"
#include "eng/core/Result.hpp"
#include "eng/fs/FileSystem.hpp"
#include "eng/fs/Path.hpp"

namespace eng::assets {

class AssetResolver final {
public:
    /// `assetsRoot` é a raiz de assets do projeto (base de todo sourcePath).
    AssetResolver(const AssetRegistry& registry, eng::fs::Path assetsRoot);

    /// Path ABSOLUTO resolvido (dentro da raiz) — sem ler o arquivo.
    [[nodiscard]] eng::core::Result<eng::fs::Path> resolve(AssetId id) const;

    /// Bytes do asset (resolve + readAllBytes).
    [[nodiscard]] eng::core::Result<std::vector<std::byte>> read(
        AssetId id, const eng::fs::FileSystem& fs) const;

    [[nodiscard]] const eng::fs::Path& assetsRoot() const noexcept
    {
        return assetsRoot_;
    }

private:
    const AssetRegistry* registry_ = nullptr;
    eng::fs::Path assetsRoot_;
};

} // namespace eng::assets
