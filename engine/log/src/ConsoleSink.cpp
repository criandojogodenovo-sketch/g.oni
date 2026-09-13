#include "eng/log/ConsoleSink.hpp"

#include <string>

namespace eng::log {

ConsoleSink::ConsoleSink(std::FILE* stream) noexcept : stream_(stream) {}

void ConsoleSink::write(LogLevel level, std::string_view category,
                        std::string_view message) {
    // Uma única escrita por linha minimiza interleaving entre threads
    // (o Logger serializa o acesso, mas o fflush de terceiros não).
    std::string line;
    line.reserve(message.size() + category.size() + toString(level).size() + 8);
    line += '[';
    line += toString(level);
    line += ']';
    line += ' ';
    line += '[';
    line += category;
    line += ']';
    line += ' ';
    line += message;
    line += '\n';

    std::fwrite(line.data(), 1, line.size(), stream_);
}

void ConsoleSink::flush() {
    std::fflush(stream_);
}

} // namespace eng::log
