#pragma once

/// eng::project::ProjectFile — parse/serialize de project.goni.json
/// (FASE 3; ADR-032).
#include <string>
#include <string_view>

#include "eng/core/Result.hpp"
#include "eng/fs/FileSystem.hpp"
#include "eng/fs/Path.hpp"
#include "eng/project/ProjectConfig.hpp"
#include "eng/project/ProjectPaths.hpp"
#include "eng/serial/JsonValue.hpp"

namespace eng::project {

class ProjectFile final {
public:
    ProjectConfig config;
    eng::fs::Path filePath; ///< onde o arquivo vive (base do ProjectPaths)

    /// Parse do CONTEÚDO (texto) de um project.goni.json que vive em
    /// `filePath`. Validações (erros claros, nunca throw):
    ///   - raiz objeto, formatVersion == kProjectFormatVersion
    ///     (maior → NotSupported);
    ///   - projectId canônico, name string;
    ///   - engineVersion "MAJOR.MINOR.PATCH";
    ///   - assetRegistryPath/sceneRoots RELATIVOS (absoluto →
    ///     InvalidArgument — regra dura da missão §2.6).
    [[nodiscard]] static eng::core::Result<ProjectFile> parse(
        const eng::fs::Path& filePath, std::string_view text);

    /// Lê de um FileSystem (texto → parse).
    [[nodiscard]] static eng::core::Result<ProjectFile> readFrom(
        const eng::fs::FileSystem& fs, const eng::fs::Path& filePath);

    /// JSON determinístico do config (round-trip byte-estável).
    [[nodiscard]] eng::core::Result<std::string> serialize() const;

    /// Escreve no FileSystem (via serialize). I/O de escrita é mutante —
    /// o FileSystem vem não-const (contrato de eng::fs).
    [[nodiscard]] eng::core::Result<void> writeTo(
        eng::fs::FileSystem& fs) const;

    /// Paths resolvidos a partir do diretório DO ARQUIVO.
    [[nodiscard]] ProjectPaths paths() const
    {
        return ProjectPaths{filePath.parent()};
    }

private:
    [[nodiscard]] static eng::core::Result<ProjectConfig> configFromJson(
        const eng::serial::JsonValue& value);
    [[nodiscard]] static eng::core::Result<eng::serial::JsonValue> toJson(
        const ProjectConfig& config);
};

} // namespace eng::project
