#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <unordered_set>

#include "eng/core/Uuid.hpp"

TEST_CASE("core: Uuid128 round-trip string e bytes", "[core][uuid]")
{
    const auto generated = eng::core::Uuid128::generate();
    const std::string text = generated.toString();

    // Forma canônica: 36 chars, minúsculas, hífens nas posições certas.
    REQUIRE(text.size() == 36);
    CHECK(text[8] == '-');
    CHECK(text[13] == '-');
    CHECK(text[18] == '-');
    CHECK(text[23] == '-');
    CHECK(text.find_first_of("ABCDEF") == std::string::npos);

    // v4 e variante visíveis na forma canônica.
    CHECK(text[14] == '4');
    CHECK(std::string("89ab").find(text[19]) != std::string::npos);

    const auto parsed = eng::core::Uuid128::fromString(text);
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == generated);

    // Binário BE: round-trip + vetor conhecido.
    const auto bytes = generated.toBytesBE();
    CHECK(bytes.size() == 16);
    const auto back = eng::core::Uuid128::fromBytesBE(bytes);
    REQUIRE(back.ok());
    CHECK(back.value() == generated);

    // Vetor: bits → bytes big-endian na ordem do texto.
    const auto known = eng::core::Uuid128::fromString(
        "00112233-4455-4677-8899-aabbccddeeff");
    REQUIRE(known.ok());
    const auto knownBytes = known.value().toBytesBE();
    const unsigned char expected[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
                                        0x46, 0x77, 0x88, 0x99, 0xaa, 0xbb,
                                        0xcc, 0xdd, 0xee, 0xff};
    for (std::size_t i = 0; i < 16; ++i) {
        CHECK(static_cast<unsigned char>(knownBytes[i]) == expected[i]);
    }
}

TEST_CASE("core: Uuid128 parse estrito rejeita desvios", "[core][uuid]")
{
    using eng::core::StatusCode;

    const auto check = [](std::string_view bad, const char* why) {
        const auto r = eng::core::Uuid128::fromString(bad);
        INFO(why << " — input: " << bad);
        CHECK(r.isError());
        CHECK(r.error().code == StatusCode::ParseError);
    };

    const eng::core::Uuid128 u = eng::core::Uuid128::generate();
    const std::string good = u.toString();

    check("", "vazio");
    check("00112233-4455-4677-8899-aabbccddeef", "35 chars");
    check(good + "0", "37 chars");
    check(std::string(good).replace(8, 1, "x"), "hífen errado");
    check(std::string(good).replace(14, 1, "5"), "versão 5");
    check(std::string(good).replace(19, 1, "c"), "variante inválida");
    check(std::string(good).replace(0, 1, "G"), "hex inválido");

    std::string upper = good;
    for (char& c : upper) {
        if (c >= 'a' && c <= 'f') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    check(upper, "maiúsculas");
    check("{" + good + "}", "chaves");
    check("urn:uuid:" + good, "prefixo urn");
    check(good.substr(0, 32), "sem hífens");

    // fromBytesBE com tamanho errado
    const std::array<std::byte, 15> shorty{};
    const auto r = eng::core::Uuid128::fromBytesBE(shorty);
    CHECK(r.isError());
    CHECK(r.error().code == StatusCode::ParseError);
}

TEST_CASE("core: Uuid128 geração — nil, unicidade e ordem", "[core][uuid]")
{
    const eng::core::Uuid128 nil;
    CHECK(nil.isNil());
    CHECK_FALSE(eng::core::Uuid128::generate().isNil());

    // Unicidade em 10k no CORE (o teste de 100k exigido pela missão vive em
    // eng::assets sobre AssetId — mesmo gerador).
    std::unordered_set<eng::core::Uuid128> seen;
    seen.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        const eng::core::Uuid128 u = eng::core::Uuid128::generate();
        CHECK(seen.insert(u).second);
    }

    // Ordem total determinística (hi, depois lo) — ordenação estável.
    const eng::core::Uuid128 a{0x0000000000000001ull, 0xFFFFFFFFFFFFFFFFull};
    const eng::core::Uuid128 b{0x0000000000000002ull, 0x0000000000000000ull};
    CHECK(a < b);
    CHECK(a == a);
    CHECK(!(b < a));
}
