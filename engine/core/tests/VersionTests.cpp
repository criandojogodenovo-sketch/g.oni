#include <catch2/catch_test_macros.hpp>

#include "eng/core/Version.hpp"

namespace {

using eng::core::StatusCode;
using eng::core::Version;

} // namespace

TEST_CASE("Version parse aceita MAJOR.MINOR.PATCH", "[core][version]") {
    const auto r = Version::parse("0.1.0");
    REQUIRE(r.ok());
    CHECK(r.value().major == 0);
    CHECK(r.value().minor == 1);
    CHECK(r.value().patch == 0);
}

TEST_CASE("Version parse aceita zeros à esquerda", "[core][version]") {
    const auto r = Version::parse("01.02.003");
    REQUIRE(r.ok());
    CHECK(r.value() == Version{1, 2, 3});
}

TEST_CASE("Version parse rejeita entradas inválidas", "[core][version]") {
    CHECK(Version::parse("").isError());
    CHECK(Version::parse("1.2").isError());
    CHECK(Version::parse("1.2.3.4").isError());
    CHECK(Version::parse("v1.2.3").isError());
    CHECK(Version::parse("1.2.x").isError());
    CHECK(Version::parse("1.2.3 ").isError());
    CHECK(Version::parse("4294967296.0.0").isError()); // uint32 + 1
}

TEST_CASE("Version parse relata o motivo da falha", "[core][version]") {
    const auto r = Version::parse("1.2");
    REQUIRE(r.isError());
    CHECK(r.error().code == StatusCode::ParseError);
    CHECK_FALSE(r.error().message.empty());
}

TEST_CASE("Version toString faz ida-e-volta", "[core][version]") {
    const auto r = Version::parse("12.34.56");
    REQUIRE(r.ok());
    CHECK(r.value().toString() == "12.34.56");
    CHECK(Version{0, 0, 0}.toString() == "0.0.0");
}

TEST_CASE("Version compara lexicograficamente", "[core][version]") {
    CHECK(Version{1, 2, 0} < Version{1, 10, 0});       // minor 2 < 10 (não string)
    CHECK(Version{2, 0, 0} > Version{1, 99, 99});
    CHECK(Version{1, 2, 3} == Version{1, 2, 3});
    CHECK(Version{1, 2, 3} <= Version{1, 2, 3});
    CHECK(Version{1, 2, 3} != Version{1, 2, 4});
}
