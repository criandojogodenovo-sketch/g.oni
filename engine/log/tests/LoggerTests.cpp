#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "eng/log/Logger.hpp"

namespace {

using eng::log::LogLevel;
using eng::log::Logger;

struct Entry {
    LogLevel level;
    std::string category;
    std::string message;
};

/// Sink de teste: grava tudo em memória para inspeção.
class MemorySink final : public eng::log::LogSink {
public:
    void write(LogLevel level, std::string_view category,
               std::string_view message) override {
        entries.push_back(Entry{level, std::string(category), std::string(message)});
    }
    void flush() override {
        ++flushCount;
    }

    std::vector<Entry> entries;
    int flushCount = 0;
};

} // namespace

TEST_CASE("Logger distribui para sinks com categoria e nível", "[log][logger]") {
    Logger logger;
    MemorySink sink;
    logger.addSink(sink);

    logger.log(LogLevel::Warn, "render", "a luz explodiu");
    REQUIRE(sink.entries.size() == 1);
    CHECK(sink.entries[0].level == LogLevel::Warn);
    CHECK(sink.entries[0].category == "render");
    CHECK(sink.entries[0].message == "a luz explodiu");
}

TEST_CASE("Logger filtra mensagens abaixo do nível mínimo", "[log][logger]") {
    Logger logger;
    MemorySink sink;
    logger.addSink(sink);

    logger.setMinLevel(LogLevel::Info); // padrão do Logger é Info
    logger.log(LogLevel::Trace, "cat", "ruidoso");
    logger.log(LogLevel::Debug, "cat", "verboso");
    CHECK(sink.entries.empty());

    logger.log(LogLevel::Info, "cat", "na medida");
    logger.log(LogLevel::Fatal, "cat", "grave");
    REQUIRE(sink.entries.size() == 2);
    CHECK(sink.entries[0].message == "na medida");
    CHECK(sink.entries[1].message == "grave");
}

TEST_CASE("Logger aceita exatamente o nível mínimo", "[log][logger]") {
    Logger logger;
    MemorySink sink;
    logger.addSink(sink);

    logger.setMinLevel(LogLevel::Warn);
    logger.log(LogLevel::Info, "cat", "abaixo");
    logger.log(LogLevel::Warn, "cat", "no limite");
    REQUIRE(sink.entries.size() == 1);
    CHECK(sink.entries[0].message == "no limite");
}

TEST_CASE("Logger registra múltiplos sinks e remove seletivamente", "[log][logger]") {
    Logger logger;
    MemorySink a;
    MemorySink b;
    logger.addSink(a);
    logger.addSink(b);

    logger.log(LogLevel::Error, "fisica", "colisão infinita");
    CHECK(a.entries.size() == 1);
    CHECK(b.entries.size() == 1);
    CHECK(logger.sinkCount() == 2);

    logger.removeSink(a);
    logger.log(LogLevel::Error, "fisica", "segunda");
    CHECK(a.entries.size() == 1); // a não recebe mais
    CHECK(b.entries.size() == 2);
    CHECK(logger.sinkCount() == 1);
}

TEST_CASE("Logger addSink é idempotente", "[log][logger]") {
    Logger logger;
    MemorySink sink;
    logger.addSink(sink);
    logger.addSink(sink);

    logger.log(LogLevel::Info, "cat", "uma vez");
    CHECK(sink.entries.size() == 1);
    CHECK(logger.sinkCount() == 1);

    logger.removeSink(sink);
    logger.removeSink(sink); // idempotente
    CHECK(logger.sinkCount() == 0);
}

TEST_CASE("Logger flush propaga para todos os sinks", "[log][logger]") {
    Logger logger;
    MemorySink a;
    MemorySink b;
    logger.addSink(a);
    logger.addSink(b);

    logger.flush();
    CHECK(a.flushCount == 1);
    CHECK(b.flushCount == 1);
}

TEST_CASE("Logger::get devolve a mesma instância global", "[log][logger]") {
    CHECK(&Logger::get() == &Logger::get());
    CHECK(Logger::get().minLevel() == LogLevel::Info);
    CHECK(Logger::get().sinkCount() == 0);
}
