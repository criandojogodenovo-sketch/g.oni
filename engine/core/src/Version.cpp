#include "eng/core/Version.hpp"

#include <cctype>
#include <cstdio>

namespace eng::core {

namespace {

[[nodiscard]] constexpr bool isDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

} // namespace

Result<Version> Version::parse(std::string_view text) {
    std::uint32_t parts[3] = {0, 0, 0};
    std::size_t pos = 0;

    for (int part = 0; part < 3; ++part) {
        if (pos >= text.size() || !isDigit(text[pos])) {
            return makeUnexpected(Error{
                StatusCode::ParseError,
                "esperava dígito no início da parte " + std::to_string(part + 1),
            });
        }

        std::uint64_t value = 0;
        while (pos < text.size() && isDigit(text[pos])) {
            value = value * 10 + static_cast<std::uint64_t>(text[pos] - '0');
            if (value > 0xFFFFFFFFull) {
                return makeUnexpected(Error{
                    StatusCode::ParseError,
                    "componente excede uint32",
                });
            }
            ++pos;
        }
        parts[part] = static_cast<std::uint32_t>(value);

        if (part < 2) {
            if (pos >= text.size() || text[pos] != '.') {
                return makeUnexpected(Error{
                    StatusCode::ParseError,
                    "esperava '.' após a parte " + std::to_string(part + 1),
                });
            }
            ++pos;
        }
    }

    if (pos != text.size()) {
        return makeUnexpected(Error{
            StatusCode::ParseError,
            "caracteres excedentes após a parte 3",
        });
    }

    return Version{parts[0], parts[1], parts[2]};
}

std::string Version::toString() const {
    char buffer[48];
    const int written = std::snprintf(buffer, sizeof(buffer), "%u.%u.%u",
                                      static_cast<unsigned>(major),
                                      static_cast<unsigned>(minor),
                                      static_cast<unsigned>(patch));
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(buffer)) {
        return "0.0.0"; // impossível na prática; caminho sem exceções
    }
    return std::string(buffer, static_cast<std::size_t>(written));
}

} // namespace eng::core
