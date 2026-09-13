#pragma once

#include <string_view>

#include "eng/log/LogLevel.hpp"

namespace eng::log {

/// Destino de mensagens de log (stdout, arquivo, logcat — §4.3).
/// Sinks recebem a mensagem já formatada; implementações devem ser thread-safe
/// por conta própria quando compartilhadas entre threads (o Logger serializa
/// o acesso com mutex, então sinks simples bastam nesta fase).
class LogSink {
public:
    virtual ~LogSink() = default;

    LogSink() = default;
    LogSink(const LogSink&) = delete;
    LogSink& operator=(const LogSink&) = delete;

    /// Registra uma mensagem (chamado sob o lock do Logger).
    virtual void write(LogLevel level, std::string_view category,
                       std::string_view message) = 0;

    /// Esvazia buffers internos; implementações sem buffer podem não fazer nada.
    virtual void flush() {}
};

} // namespace eng::log
