#pragma once

#include <cstdio>

#include "eng/log/LogSink.hpp"

namespace eng::log {

/// Sink para FILE* (stdout/stderr ou arquivo aberto pelo dono).
/// Formato por linha: "[LEVEL] [categoria] mensagem\n".
///
/// O stream NÃO é de propriedade do sink — quem abriu, fecha. Isso permite
/// testes com tmpfile() e composição com rotação futura.
class ConsoleSink final : public LogSink {
public:
    /// `stream` deve permanecer vivo enquanto o sink existir.
    explicit ConsoleSink(std::FILE* stream = stdout) noexcept;

    void write(LogLevel level, std::string_view category,
               std::string_view message) override;
    void flush() override;

private:
    std::FILE* stream_;
};

} // namespace eng::log
