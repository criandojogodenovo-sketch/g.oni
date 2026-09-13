#pragma once

#include <mutex>
#include <vector>

#include "eng/log/LogLevel.hpp"
#include "eng/log/LogSink.hpp"

namespace eng::log {

/// Logger central: filtra por nível mínimo e fan-out para sinks.
///
/// - Sinks são ponteiros NÃO donos (quem registrou, remove).
/// - `log()` não aloca: a formatação acontece na macro (ENG_LOG) ANTES da
///   chamada, já com curto-circuito do nível — o caminho filtrado é custo zero.
/// - Thread-safe (mutex interno). A ordem das linhas entre threads é arbitrária.
/// - `Logger::get()` devolve a instância global do processo.
class Logger {
public:
    static Logger& get() noexcept;

    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /// Registra um sink (idempotente: duplicatas são ignoradas).
    void addSink(LogSink& sink);

    /// Remove um sink previamente registrado (idempotente).
    void removeSink(LogSink& sink);

    /// Nível mínimo aceito (padrão: Info).
    void setMinLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel minLevel() const noexcept;

    /// Publica uma mensagem (categoria e mensagem já montados).
    void log(LogLevel level, std::string_view category, std::string_view message);

    /// Esvazia todos os sinks registrados.
    void flush();

    /// Quantidade de sinks registrados (diagnóstico/teste).
    [[nodiscard]] std::size_t sinkCount() const noexcept;

private:
    mutable std::mutex mutex_;
    std::vector<LogSink*> sinks_;
    LogLevel minLevel_ = LogLevel::Info;
};

} // namespace eng::log
