#include "eng/platform/ProcessInfo.hpp"

#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <string>

namespace eng::platform {

eng::core::Result<eng::fs::Path> ProcessInfo::currentExecutablePath()
{
    std::array<char, 4096> buffer{};
    const ssize_t len = readlink("/proc/self/exe", buffer.data(),
                                 buffer.size() - 1);
    if (len <= 0) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::IOError,
            std::string("currentExecutablePath: readlink(/proc/self/exe) "
                        "falhou: ") +
                std::strerror(errno)});
    }
    buffer[static_cast<std::size_t>(len)] = '\0';
    return eng::fs::Path{std::string_view{buffer.data(),
                                           static_cast<std::size_t>(len)}};
}

} // namespace eng::platform
