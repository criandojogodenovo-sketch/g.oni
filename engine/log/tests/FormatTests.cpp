#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>

#include "eng/log/Format.hpp"

namespace {

template <typename... Args>
std::string fmt(std::string_view pattern, const Args&... args) {
    return eng::log::format(pattern, args...);
}

} // namespace

TEST_CASE("format substitui placeholders em ordem", "[log][format]") {
    CHECK(fmt("x={} y={}", 1, 2) == "x=1 y=2");
    CHECK(fmt("{}-{}", "alpha", "beta") == "alpha-beta");
    CHECK(fmt("sem placeholders") == "sem placeholders");
    CHECK(fmt("vazio") == "vazio");
}

TEST_CASE("format aceita um único argumento", "[log][format]") {
    CHECK(fmt("valor: {}", 42) == "valor: 42");
    CHECK(fmt("{}", "somente") == "somente");
}

TEST_CASE("format escapa {{ e }} literalmente", "[log][format]") {
    CHECK(fmt("literal {{ chaves") == "literal { chaves");
    CHECK(fmt("fechado }} aqui") == "fechado } aqui");
    CHECK(fmt("chaves {{}} vazias") == "chaves {} vazias");
    CHECK(fmt("{{}}") == "{}");
}

TEST_CASE("format deixa placeholder excedente literal", "[log][format]") {
    CHECK(fmt("{} e {}", 1) == "1 e {}");
    CHECK(fmt("{} {}", 1, 2, 3) == "1 2"); // argumentos excedentes ignorados
}

TEST_CASE("format tipifica valores comuns", "[log][format]") {
    CHECK(fmt("b={}", true) == "b=true");
    CHECK(fmt("b={}", false) == "b=false");
    CHECK(fmt("c={}", 'x') == "c=x");
    CHECK(fmt("n={}", -7) == "n=-7");
    CHECK(fmt("u={}", 42u) == "u=42");
    CHECK(fmt("f={}", 3.5) == "f=3.5");
    CHECK(fmt("pi={}", 3.14159265) == "pi=3.14159");
}

TEST_CASE("format trata int8/uint8 como números (não como caracteres)", "[log][format]") {
    const std::int8_t signedByte = 65;
    const std::uint8_t unsignedByte = 200;
    CHECK(fmt("{}", signedByte) == "65");
    CHECK(fmt("{}", unsignedByte) == "200");
}

TEST_CASE("format aceita string_view, string e literais", "[log][format]") {
    const std::string_view view{"view"};
    const std::string owned{"owned"};
    CHECK(fmt("{}", view) == "view");
    CHECK(fmt("{}", owned) == "owned");
    CHECK(fmt("{}", "literal") == "literal");
}

TEST_CASE("format converte const char* nulo em (null)", "[log][format]") {
    const char* nullPtr = nullptr;
    CHECK(fmt("{}", nullPtr) == "(null)");
}

TEST_CASE("format aceita tipos grandes sem estourar", "[log][format]") {
    CHECK(fmt("{}", 4294967295u) == "4294967295");
    CHECK(fmt("{}", -9007199254740993LL) == "-9007199254740993");
    CHECK(fmt("{}", 0.125) == "0.125");
    CHECK(fmt("{}", 1e+25) == "1e+25");
}

TEST_CASE("format mistura escapes e placeholders", "[log][format]") {
    CHECK(fmt("{{ }} {}", 9) == "{ } 9");
    CHECK(fmt("a {{\nb={}", 5) == "a {\nb=5");
}
