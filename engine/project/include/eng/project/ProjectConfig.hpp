#pragma once

/// eng::project::ProjectConfig — metadados do projeto (FASE 3; ADR-032).
///
/// REGRA DURA (missão §2.6/R10): nenhuma string ABSOLUTA pode ser
/// persistida — assetRegistryPath e sceneRoots são relativos ao diretório
/// do arquivo de projeto (project.goni.json); o parse REJEITA absolutos.
#include <cstdint>
#include <string>
#include <vector>

#include "eng/core/Version.hpp"
#include "eng/fs/Path.hpp"
#include "eng/project/ProjectId.hpp"

namespace eng::project {

inline constexpr std::uint32_t kProjectFormatVersion = 1;

struct ProjectConfig {
    ProjectId projectId;
    std::string name;
    eng::core::Version engineVersion;      ///< versão do motor que escreveu
    eng::fs::Path assetRegistryPath;       ///< relativo (ex.: "asset_registry.json")
    std::vector<eng::fs::Path> sceneRoots;  ///< relativos (ex.: "assets/scenes")

    [[nodiscard]] bool operator==(const ProjectConfig&) const = default;
};

} // namespace eng::project
