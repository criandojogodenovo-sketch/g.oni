#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstring>
#include <vector>

#include "eng/image/Image.hpp"

#include "ImageFixtures.hpp"

using eng::image::ImageFormat;
using eng::image::detectFormat;
using eng::image::decode;

namespace {

std::byte b(int v) { return static_cast<std::byte>(v); }

}  // namespace

// =============================================================================
// Detecção de formato
// =============================================================================

TEST_CASE("image: detectFormat por magic bytes", "[image]")
{
    // PNG válido (prefixo).
    const std::byte png[] = {b(0x89), b('P'), b('N'), b('G'), b(0x0D),
                             b(0x0A), b(0x1A), b(0x0A)};
    CHECK(detectFormat(std::span{png}) == ImageFormat::Png);

    // JPEG válido (prefixo).
    const std::byte jpeg[] = {b(0xFF), b(0xD8), b(0xFF), b(0xE0)};
    CHECK(detectFormat(std::span{jpeg}) == ImageFormat::Jpeg);

    // Desconhecidos: vazio, texto, BMP, GIF.
    CHECK(detectFormat({}) == ImageFormat::Unknown);
    const std::byte text[] = {b('h'), b('e'), b('l'), b('l'), b('o')};
    CHECK(detectFormat(std::span{text}) == ImageFormat::Unknown);
    const std::byte bmp[] = {b('B'), b('M')};
    CHECK(detectFormat(std::span{bmp}) == ImageFormat::Unknown);
    const std::byte gif[] = {b('G'), b('I'), b('F'), b('8')};
    CHECK(detectFormat(std::span{gif}) == ImageFormat::Unknown);

    // Prefixos INCOMPLETOS de PNG não detectam (precisa dos 8 bytes).
    const std::byte pngCurto[] = {b(0x89), b('P'), b('N')};
    CHECK(detectFormat(std::span{pngCurto}) == ImageFormat::Unknown);
}

// =============================================================================
// Decodificação PNG REAL
// =============================================================================

TEST_CASE("image: decodifica PNG 2x2 com cores exatas", "[image]")
{
    const auto data = eng::image::testing::png2x2Bytes();
    auto decoded = decode(data);
    REQUIRE(decoded.ok());

    const auto& image = decoded.value();
    CHECK(image.width == 2);
    CHECK(image.height == 2);
    CHECK(image.sourceChannels == 4);  // arquivo RGBA
    CHECK(image.alpha);
    REQUIRE(image.pixels.size() == 2 * 2 * 4);

    // Pixels EXATOS (linha-a-linha, row-major): (0,0)=vermelho,
    // (1,0)=verde, (0,1)=azul, (1,1)=branco.
    const auto px = [&image](std::size_t i) {
        return static_cast<unsigned char>(image.pixels[i]);
    };
    CHECK(px(0) == 255);  CHECK(px(1) == 0);    CHECK(px(2) == 0);    CHECK(px(3) == 255);
    CHECK(px(4) == 0);    CHECK(px(5) == 255);  CHECK(px(6) == 0);    CHECK(px(7) == 255);
    CHECK(px(8) == 0);    CHECK(px(9) == 0);    CHECK(px(10) == 255);  CHECK(px(11) == 255);
    CHECK(px(12) == 255); CHECK(px(13) == 255); CHECK(px(14) == 255);  CHECK(px(15) == 255);
}

TEST_CASE("image: decodifica JPEG 4x4 (perda aceitável, dimensões exatas)",
          "[image]")
{
    const auto data = eng::image::testing::jpeg4x4Bytes();
    auto decoded = decode(data);
    REQUIRE(decoded.ok());

    const auto& image = decoded.value();
    CHECK(image.width == 4);
    CHECK(image.height == 4);
    CHECK(image.sourceChannels == 3);  // arquivo RGB
    CHECK_FALSE(image.alpha);           // JPEG não tem alfa
    CHECK(image.pixels.size() == 4 * 4 * 4);  // saída SEMPRE RGBA8

    // Cor coral (250, 120, 80) com JPEG lossy: tolerância por canal (±32).
    for (std::size_t i = 0; i < 4 * 4; ++i) {
        const auto r = static_cast<int>(image.pixels[i * 4 + 0]);
        const auto g = static_cast<int>(image.pixels[i * 4 + 1]);
        const auto bl = static_cast<int>(image.pixels[i * 4 + 2]);
        CHECK(r > 250 - 32);
        CHECK(g > 120 - 32);
        CHECK(g < 120 + 32);
        CHECK(bl > 80 - 32);
        CHECK(bl < 80 + 32);
        CHECK(image.pixels[i * 4 + 3] == std::byte{255});  // alfa opaco
    }
}

// =============================================================================
// Erros precisos
// =============================================================================

TEST_CASE("image: erros precisos de decodificação", "[image]")
{
    SECTION("dados vazios") {
        auto bad = decode({});
        REQUIRE(bad.isError());
        CHECK(bad.error().message.find("vazios") != std::string::npos);
    }

    SECTION("formato desconhecido") {
        const std::byte lixo[] = {b(1), b(2), b(3), b(4)};
        auto bad = decode(std::span{lixo});
        REQUIRE(bad.isError());
        CHECK(bad.error().message.find("formato não reconhecido") != std::string::npos);
    }

    SECTION("PNG truncado — motivo do decodificador incluído") {
        const auto data = eng::image::testing::png2x2Bytes();
        // Corta no meio do IDAT: magic PNG ok, corpo incompleto.
        auto truncated = data.subspan(0, 40);
        auto bad = decode(truncated);
        REQUIRE(bad.isError());
        CHECK(bad.error().message.find("decodificação falhou") != std::string::npos);
    }

    SECTION("magic JPEG com corpo lixo") {
        const std::byte fake[] = {b(0xFF), b(0xD8), b(0xFF), b(0x00), b(0x01),
                                  b(0x02), b(0x03), b(0x04)};
        auto bad = decode(std::span{fake});
        REQUIRE(bad.isError());
    }
}

// =============================================================================
// Contrato RGBA8 (paridade com TextureDesc do RHI)
// =============================================================================

TEST_CASE("image: saída é sempre RGBA8 tight-packed (contrato TextureDesc)",
          "[image]")
{
    const auto data = eng::image::testing::png2x2Bytes();
    auto decoded = decode(data);
    REQUIRE(decoded.ok());
    const auto& image = decoded.value();
    CHECK(image.expectedSize() == image.pixels.size());
    // PNG 2x2 → 16 bytes (nada de padding de linha, nada de BGRA).
    CHECK(image.pixels.size() == 16);
}
