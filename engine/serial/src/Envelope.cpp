#include "eng/serial/Envelope.hpp"

#include <algorithm>
#include <array>

#include "eng/serial/Binary.hpp"

namespace eng::serial {

namespace {

/// Tabela do CRC-32/ISO-HDLC (poly refletido 0xEDB88320).
struct Crc32Table {
    std::array<std::uint32_t, 256> entries{};

    constexpr Crc32Table()
    {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) != 0 ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            entries[i] = c;
        }
    }
};

constexpr Crc32Table kCrc32Table{};

} // namespace

std::uint32_t crc32(std::span<const std::byte> data)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const std::byte b : data) {
        crc = kCrc32Table.entries[(crc ^ static_cast<std::uint8_t>(b)) &
                                  0xFFu] ^
              (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::vector<std::byte> encodeEnvelope(std::uint32_t assetType,
                                      std::uint32_t payloadVersion,
                                      std::span<const std::byte> payload)
{
    std::vector<std::byte> out;
    out.reserve(payload.size() + kEnvelopeOverhead);

    BinaryWriter w(out);
    w.bytes(kEnvelopeMagic);
    w.u32BE(kEnvelopeFormatVersion);
    w.u32BE(assetType);
    w.u32BE(payloadVersion);
    w.u64BE(payload.size());
    w.bytes(payload);

    const std::uint32_t crc = crc32(out);
    w.u32BE(crc);
    return out;
}

eng::core::Result<Envelope> decodeEnvelope(std::span<const std::byte> whole)
{
    using eng::core::Error;
    using eng::core::StatusCode;
    using eng::core::makeUnexpected;

    if (whole.size() < kEnvelopeOverhead) {
        return makeUnexpected(Error{
            StatusCode::ParseError,
            "decodeEnvelope: " + std::to_string(whole.size()) +
                " bytes é menor que o envelope mínimo (" +
                std::to_string(kEnvelopeOverhead) + ")"});
    }

    BinaryReader r(whole);

    std::array<std::byte, 4> magic{};
    {
        const auto got = r.bytes(4);
        if (got.isError()) {
            return makeUnexpected(got.error());
        }
        std::copy(got.value().begin(), got.value().end(), magic.begin());
    }
    if (magic != kEnvelopeMagic) {
        return makeUnexpected(
            Error{StatusCode::ParseError, "decodeEnvelope: magic errado"});
    }

    const auto formatVersion = r.u32BE();
    if (formatVersion.isError()) {
        return makeUnexpected(formatVersion.error());
    }
    if (formatVersion.value() > kEnvelopeFormatVersion) {
        return makeUnexpected(Error{
            StatusCode::NotSupported,
            "decodeEnvelope: formatVersion " +
                std::to_string(formatVersion.value()) +
                " é maior que o suportado (" +
                std::to_string(kEnvelopeFormatVersion) + ")"});
    }
    if (formatVersion.value() == 0) {
        return makeUnexpected(Error{StatusCode::ParseError,
                                    "decodeEnvelope: formatVersion 0"});
    }

    const auto assetType = r.u32BE();
    if (assetType.isError()) {
        return makeUnexpected(assetType.error());
    }
    const auto payloadVersion = r.u32BE();
    if (payloadVersion.isError()) {
        return makeUnexpected(payloadVersion.error());
    }
    const auto payloadSize = r.u64BE();
    if (payloadSize.isError()) {
        return makeUnexpected(payloadSize.error());
    }

    // Consistência de tamanho: payload + crc exatamente até o fim.
    const std::uint64_t expectedRest =
        payloadSize.value() + sizeof(std::uint32_t);
    if (r.remaining() != expectedRest) {
        return makeUnexpected(Error{
            StatusCode::ParseError,
            "decodeEnvelope: payloadSize " +
                std::to_string(payloadSize.value()) +
                " inconsistente com o tamanho total (restam " +
                std::to_string(r.remaining()) + ", esperados " +
                std::to_string(expectedRest) + ")"});
    }

    Envelope envelope;
    envelope.assetType = assetType.value();
    envelope.payloadVersion = payloadVersion.value();

    const auto payload = r.bytes(static_cast<std::size_t>(payloadSize.value()));
    if (payload.isError()) {
        return makeUnexpected(payload.error());
    }
    envelope.payload = std::move(payload.value());

    const auto storedCrc = r.u32BE();
    if (storedCrc.isError()) {
        return makeUnexpected(storedCrc.error());
    }

    // CRC cobre TUDO antes do próprio campo.
    const std::uint32_t computed =
        crc32(whole.first(whole.size() - sizeof(std::uint32_t)));
    if (computed != storedCrc.value()) {
        return makeUnexpected(Error{
            StatusCode::ParseError,
            "decodeEnvelope: CRC32 divergente (armazenado 0x" +
                std::to_string(storedCrc.value()) + ", calculado 0x" +
                std::to_string(computed) + ")"});
    }

    return envelope;
}

} // namespace eng::serial
