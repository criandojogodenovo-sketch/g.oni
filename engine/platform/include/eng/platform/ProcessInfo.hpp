#pragma once

/// eng::platform::ProcessInfo — fatos do processo corrente (FASE 3; ADR-026).
#include "eng/core/Result.hpp"
#include "eng/fs/Path.hpp"

namespace eng::platform {

struct ProcessInfo final {
    ProcessInfo() = delete;

    /// Caminho absoluto do executável em execução (Linux: /proc/self/exe).
    /// Erro: IOError com mensagem do readlink.
    [[nodiscard]] static eng::core::Result<eng::fs::Path>
    currentExecutablePath();
};

} // namespace eng::platform
