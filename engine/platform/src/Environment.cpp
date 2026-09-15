#include "eng/platform/Environment.hpp"

#include <cstdlib>
#include <string>

namespace eng::platform {

std::optional<std::string> Environment::get(std::string_view name)
{
    if (name.empty()) {
        return std::nullopt;
    }
    const std::string key{name};
    const char* value = std::getenv(key.c_str());
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string{value};
}

bool Environment::set(std::string_view name, std::string_view value,
                      bool overwrite)
{
    if (name.empty() || name.find('=') != std::string_view::npos) {
        return false;
    }
    const std::string key{name};
    const std::string val{value};
    return setenv(key.c_str(), val.c_str(), overwrite ? 1 : 0) == 0;
}

bool Environment::unset(std::string_view name)
{
    if (name.empty() || name.find('=') != std::string_view::npos) {
        return false;
    }
    if (!get(name).has_value()) {
        return false;
    }
    const std::string key{name};
    unsetenv(key.c_str());
    return true;
}

} // namespace eng::platform
