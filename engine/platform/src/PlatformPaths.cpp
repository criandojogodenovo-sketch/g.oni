#include "eng/platform/PlatformPaths.hpp"

#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <string>

#include "eng/log/Macros.hpp"
#include "eng/platform/Environment.hpp"
#include "eng/platform/ProcessInfo.hpp"

ENG_LOG_CATEGORY("platform");

namespace eng::platform {

namespace {

/// Base XDG de duas letras: variável → fallback sob $HOME.
[[nodiscard]] std::string xdgBase(const char* var, const char* homeSuffix,
                                  std::string& homeFallbackNotice)
{
    if (const auto value = Environment::get(var); value.has_value() &&
                                                   !value->empty()) {
        return *value;
    }
    if (const auto home = Environment::get("HOME"); home.has_value() &&
                                                    !home->empty()) {
        return *home + homeSuffix;
    }
    homeFallbackNotice = var;
    return "/tmp/goni-fallback"; // documentado em ADR-026 + warn abaixo
}

} // namespace

PlatformPaths PlatformPaths::detect(std::string_view appName)
{
    std::string fallbackNotice;

    const std::string userDataBase =
        xdgBase("XDG_DATA_HOME", "/.local/share", fallbackNotice);
    const std::string cacheBase =
        xdgBase("XDG_CACHE_HOME", "/.cache", fallbackNotice);

    std::string temp = "/tmp";
    if (const auto tmpdir = Environment::get("TMPDIR");
        tmpdir.has_value() && !tmpdir->empty()) {
        temp = *tmpdir;
    }

    if (!fallbackNotice.empty()) {
        ENG_WARN("sem XDG/HOME para {} — raízes sob /tmp/goni-fallback",
                 fallbackNotice);
    }

    PlatformPaths paths;
    paths.userDataRoot =
        eng::fs::Path{userDataBase} / eng::fs::Path{appName};
    paths.cacheRoot = eng::fs::Path{cacheBase} / eng::fs::Path{appName};
    paths.tempRoot = eng::fs::Path{temp};

    if (const auto exe = ProcessInfo::currentExecutablePath(); exe.ok()) {
        paths.executableRoot = exe.value().parent();
    } else {
        // Fallback documentado: sem /proc (container mínimo?) — cwd.
        std::array<char, 1024> cwd{};
        if (getcwd(cwd.data(), cwd.size()) != nullptr) {
            paths.executableRoot = eng::fs::Path{cwd.data()};
        } else {
            paths.executableRoot = eng::fs::Path{"/tmp"};
        }
        ENG_WARN("currentExecutablePath falhou: {} — executableRoot=cwd",
                 exe.error().message);
    }
    return paths;
}

} // namespace eng::platform
