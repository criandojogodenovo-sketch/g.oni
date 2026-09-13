#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>
#include <string>

#include "eng/log/ConsoleSink.hpp"
#include "eng/log/Macros.hpp"

namespace {

using eng::log::ConsoleSink;
using eng::log::LogLevel;

/// ENG_LOG_CATEGORY no escopo deste TU: os macros abaixo dependem dela.
ENG_LOG_CATEGORY("teste")

/// Lê todo o conteúdo do FILE* desde o início; volta ao início ao sair.
std::string drainFile(std::FILE* file) {
    std::string contents;
    std::rewind(file); // sempre drena do início, independentemente da posição
    char buffer[256];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        contents.append(buffer, n);
    }
    std::rewind(file);
    return contents;
}

} // namespace

TEST_CASE("ConsoleSink escreve linhas no formato canônico", "[log][console]") {
    std::FILE* file = std::tmpfile();
    REQUIRE(file != nullptr);

    ConsoleSink sink{file};
    sink.write(LogLevel::Info, "render", "frame 16ms");
    sink.flush();

    std::rewind(file);
    const std::string written = drainFile(file);
    CHECK(written == "[INFO] [render] frame 16ms\n");

    sink.write(LogLevel::Error, "rhi", "vkQueueSubmit falhou");
    sink.flush();
    const std::string more = drainFile(file);
    CHECK(more.find("[ERROR] [rhi] vkQueueSubmit falhou\n") != std::string::npos);

    std::fclose(file);
}

TEST_CASE("ConsoleSink suporta mensagem e categoria vazias", "[log][console]") {
    std::FILE* file = std::tmpfile();
    REQUIRE(file != nullptr);

    ConsoleSink sink{file};
    sink.write(LogLevel::Warn, "", "");
    sink.flush();

    std::rewind(file);
    const std::string written = drainFile(file);
    CHECK(written == "[WARN] [] \n");

    std::fclose(file);
}

TEST_CASE("Macros ENG_* usam a categoria do TU e a instância global", "[log][macros]") {
    std::FILE* file = std::tmpfile();
    REQUIRE(file != nullptr);

    auto& global = eng::log::Logger::get();
    ConsoleSink sink{file};
    global.addSink(sink);

    // Macacos no nível Trace exigem reduzir o mínimo globalmente.
    const auto previousLevel = global.minLevel();
    global.setMinLevel(LogLevel::Trace);

    ENG_TRACE("trace {}", 1);
    ENG_DEBUG("debug {}", 2);
    ENG_INFO("info {}", 3);
    ENG_WARN("warn {}", 4);
    ENG_ERROR("error {}", 5);
    ENG_FATAL("fatal {}", 6);

    global.setMinLevel(previousLevel);
    global.flush();
    global.removeSink(sink);

    std::rewind(file);
    const std::string written = drainFile(file);
    CHECK(written.find("[TRACE] [teste] trace 1\n") != std::string::npos);
    CHECK(written.find("[DEBUG] [teste] debug 2\n") != std::string::npos);
    CHECK(written.find("[INFO] [teste] info 3\n") != std::string::npos);
    CHECK(written.find("[WARN] [teste] warn 4\n") != std::string::npos);
    CHECK(written.find("[ERROR] [teste] error 5\n") != std::string::npos);
    CHECK(written.find("[FATAL] [teste] fatal 6\n") != std::string::npos);

    std::fclose(file);
    CHECK(eng::log::Logger::get().sinkCount() == 0);
}

TEST_CASE("Macros ENG_* respeitam o nível mínimo global (custo zero)", "[log][macros]") {
    std::FILE* file = std::tmpfile();
    REQUIRE(file != nullptr);

    auto& global = eng::log::Logger::get();
    ConsoleSink sink{file};
    global.addSink(sink);
    // Nível padrão Info: Trace e Debug são descartados antes de formatar.
    ENG_TRACE("isto {} não {} deve {} chegar", 1, 2, 3);
    ENG_INFO("isto {} chega", 4);
    global.flush();
    global.removeSink(sink);

    std::rewind(file);
    const std::string written = drainFile(file);
    CHECK(written.find("[TRACE] [teste]") == std::string::npos); // descartado antes de formatar
    CHECK(written.find("[INFO] [teste] isto 4 chega\n") != std::string::npos);

    std::fclose(file);
    CHECK(eng::log::Logger::get().sinkCount() == 0);
}
