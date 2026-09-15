#include "eng/project/ProjectPaths.hpp"

#include "eng/platform/PlatformPaths.hpp"

namespace eng::project {

ProjectPaths::ProjectPaths(eng::fs::Path projectDir)
    : projectDir_(std::move(projectDir))
{
}

ProjectPaths ProjectPaths::systemDefaults(std::string_view appName)
{
    const eng::platform::PlatformPaths system =
        eng::platform::PlatformPaths::detect(appName);
    ProjectPaths defaults{system.cacheRoot};
    return defaults;
}

eng::fs::Path ProjectPaths::assetsRoot() const
{
    return resolve(eng::fs::Path{"assets"});
}

eng::fs::Path ProjectPaths::cacheRoot() const
{
    return resolve(eng::fs::Path{"cache"});
}

eng::fs::Path ProjectPaths::buildRoot() const
{
    return resolve(eng::fs::Path{"build"});
}

eng::fs::Path ProjectPaths::resolve(const eng::fs::Path& relative) const
{
    return (projectDir_ / relative).normalized();
}

std::vector<eng::fs::Path> ProjectPaths::sceneRoots(
    const std::vector<eng::fs::Path>& configRoots) const
{
    std::vector<eng::fs::Path> resolved;
    resolved.reserve(configRoots.size());
    for (const eng::fs::Path& root : configRoots) {
        resolved.push_back(resolve(root));
    }
    return resolved;
}

} // namespace eng::project
