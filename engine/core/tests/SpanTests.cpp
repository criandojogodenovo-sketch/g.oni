#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>

#include "eng/core/Span.hpp"

namespace {

using eng::core::asBytes;
using eng::core::asSpan;
using eng::core::ByteSpan;

} // namespace

TEST_CASE("Span alias aponta para dados contíguos", "[core][span]") {
    const int dados[4] = {10, 20, 30, 40};
    const eng::core::Span<const int> s{dados, 4};
    CHECK(s.size() == 4);
    CHECK(s.data() == dados);
    CHECK(s[2] == 30);
    CHECK(s.first(2).size() == 2);
}

TEST_CASE("asBytes expõe os bytes de tipos triviais", "[core][span]") {
    const std::uint32_t valor = 0x11223344u;
    const ByteSpan bytes = asBytes(valor);
    CHECK(bytes.size() == sizeof(std::uint32_t));
    CHECK(bytes.data() != nullptr);

    const float f = 1.0f;
    CHECK(asBytes(f).size() == sizeof(float));
}

TEST_CASE("asSpan cobre um string_view sem terminador", "[core][span]") {
    constexpr std::string_view texto = "engine";
    constexpr auto s = asSpan(texto);
    static_assert(s.size() == 6);
    CHECK(s.size() == 6);
    CHECK(s.front() == 'e');
    CHECK(s.back() == 'e');
    CHECK(s.subspan(0, 3).size() == 3);
    CHECK(std::string_view{s.subspan(0, 3).data(), 3} == "eng");
}
