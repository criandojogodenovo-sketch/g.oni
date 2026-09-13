#include "eng/log/Format.hpp"

#include <cstdio>

namespace eng::log::detail {

void formatAppend(std::string& out, std::string_view rest) {
    // Caso base (sem argumentos restantes): processa apenas os escapes
    // "{{" e "}}"; "{}" sobrevive literal (documentado no header).
    std::size_t i = 0;
    while (i < rest.size()) {
        const char c = rest[i];
        const bool hasNext = (i + 1 < rest.size());
        if (c == '{' && hasNext && rest[i + 1] == '{') {
            out.push_back('{');
            i += 2;
        } else if (c == '}' && hasNext && rest[i + 1] == '}') {
            out.push_back('}');
            i += 2;
        } else {
            out.push_back(c);
            ++i;
        }
    }
}

void appendInteger(std::string& out, long long value) {
    char buffer[32];
    const int written = std::snprintf(buffer, sizeof(buffer), "%lld", value);
    if (written > 0) {
        out.append(buffer, static_cast<std::size_t>(written));
    }
}

void appendUnsigned(std::string& out, unsigned long long value) {
    char buffer[32];
    const int written = std::snprintf(buffer, sizeof(buffer), "%llu", value);
    if (written > 0) {
        out.append(buffer, static_cast<std::size_t>(written));
    }
}

void appendFloating(std::string& out, double value) {
    char buffer[48];
    const int written = std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    if (written > 0) {
        out.append(buffer, static_cast<std::size_t>(written));
    }
}

} // namespace eng::log::detail
