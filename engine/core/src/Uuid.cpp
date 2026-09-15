#include "eng/core/Uuid.hpp"

#include <random>

namespace eng::core {

namespace {

[[nodiscard]] char hexDigit(unsigned value) noexcept
{
    return static_cast<char>("0123456789abcdef"[value & 0xF]);
}

/// Valor de dígito hex minúsculo; -1 se inválido.
[[nodiscard]] int hexValue(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

/// Layout: bytes big-endian 0..15; hi = bytes 0..7, lo = bytes 8..15.
void writeBytesBE(const Uuid128& u, std::array<std::byte, 16>& out) noexcept
{
    for (std::size_t i = 0; i < 8; ++i) {
        out[i] = static_cast<std::byte>(u.hi >> (56 - 8 * i));
        out[8 + i] = static_cast<std::byte>(u.lo >> (56 - 8 * i));
    }
}

[[nodiscard]] Uuid128 readBytesBE(const std::array<std::byte, 16>& b) noexcept
{
    Uuid128 u;
    for (std::size_t i = 0; i < 8; ++i) {
        u.hi = (u.hi << 8) |
               static_cast<std::uint64_t>(static_cast<unsigned char>(b[i]));
        u.lo = (u.lo << 8) |
               static_cast<std::uint64_t>(static_cast<unsigned char>(b[8 + i]));
    }
    return u;
}

} // namespace

std::string Uuid128::toString() const
{
    std::array<std::byte, 16> bytes{};
    writeBytesBE(*this, bytes);

    std::string text;
    text.reserve(36);
    for (std::size_t i = 0; i < 16; ++i) {
        const unsigned value =
            static_cast<unsigned char>(bytes[i]);
        text.push_back(hexDigit(value >> 4));
        text.push_back(hexDigit(value));
        if (i == 3 || i == 5 || i == 7 || i == 9) {
            text.push_back('-');
        }
    }
    return text;
}

Result<Uuid128> Uuid128::fromString(std::string_view text)
{
    auto fail = [](std::string reason) {
        return makeUnexpected(
            Error{StatusCode::ParseError, "Uuid128: " + std::move(reason)});
    };

    if (text.size() != 36) {
        return fail("tamanho " + std::to_string(text.size()) +
                    " (esperado 36)");
    }
    for (const std::size_t dash : {std::size_t{8}, std::size_t{13},
                                   std::size_t{18}, std::size_t{23}}) {
        if (text[dash] != '-') {
            return fail("hífen esperado na posição " +
                        std::to_string(dash));
        }
    }
    std::array<std::byte, 16> bytes{};
    std::size_t byteIndex = 0;
    int high = -1;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            continue;
        }
        const int value = hexValue(text[i]);
        if (value < 0) {
            return fail(std::string("caractere '") + text[i] +
                        "' não é hex minúsculo na posição " +
                        std::to_string(i));
        }
        if (high < 0) {
            high = value;
        } else {
            bytes[byteIndex++] = static_cast<std::byte>((high << 4) | value);
            high = -1;
        }
    }

    const Uuid128 u = readBytesBE(bytes);

    // Validar versão 4 (nibble alto do byte 6) e variante RFC 4122
    // (bits altos do byte 8 = 10xx).
    if (((static_cast<unsigned char>(bytes[6]) >> 4) & 0xF) != 4) {
        return fail("versão != 4 (não é UUIDv4)");
    }
    const unsigned variantBits =
        (static_cast<unsigned char>(bytes[8]) >> 6) & 0x3;
    if (variantBits != 0b10) {
        return fail("variante != RFC 4122");
    }
    return u;
}

std::array<std::byte, 16> Uuid128::toBytesBE() const noexcept
{
    std::array<std::byte, 16> out{};
    writeBytesBE(*this, out);
    return out;
}

Result<Uuid128> Uuid128::fromBytesBE(std::span<const std::byte> bytes)
{
    if (bytes.size() != 16) {
        return makeUnexpected(Error{
            StatusCode::ParseError,
            "Uuid128::fromBytesBE: esperados 16 bytes, recebidos " +
                std::to_string(bytes.size())});
    }
    std::array<std::byte, 16> copy{};
    for (std::size_t i = 0; i < 16; ++i) {
        copy[i] = bytes[i];
    }
    const Uuid128 u = readBytesBE(copy);
    // Mesma validação de v4/variante do fromString (mesma identidade).
    const auto text = u.toString();
    const auto roundTrip = fromString(text);
    if (roundTrip.isError()) {
        return makeUnexpected(roundTrip.error());
    }
    return u;
}

Uuid128 Uuid128::generate()
{
    // Engine por thread: semeada uma vez por thread via random_device.
    // 122 bits aleatórios; versão/variante fixados abaixo.
    thread_local std::mt19937_64 engine{std::random_device{}()};

    Uuid128 u;
    u.hi = engine();
    u.lo = engine();
    // Versão 4: nibble ALTO do byte 6 = bits 12..15 de hi (BE: hi = bytes 0..7).
    u.hi = (u.hi & 0xFFFF'FFFF'FFFF'0FFFull) | 0x0000'0000'0000'4000ull;
    // Variante RFC 4122 (10xx): bits altos do byte 8 = bits 62..63 de lo.
    u.lo = (u.lo & 0x3FFF'FFFF'FFFF'FFFFull) | 0x8000'0000'0000'0000ull;
    return u;
}

} // namespace eng::core
