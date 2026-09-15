#include "eng/serial/Binary.hpp"

#include <cstring>
#include <utility>

namespace eng::serial {

namespace {

void pushBE(std::vector<std::byte>& out, std::uint64_t value,
            std::size_t width)
{
    for (std::size_t i = 0; i < width; ++i) {
        out.push_back(
            static_cast<std::byte>((value >> (8 * (width - 1 - i))) & 0xFF));
    }
}

} // namespace

// =============================================================================
// BinaryWriter
// =============================================================================

void BinaryWriter::u8(std::uint8_t value)
{
    out_.push_back(static_cast<std::byte>(value));
}

void BinaryWriter::u16BE(std::uint16_t value) { pushBE(out_, value, 2); }
void BinaryWriter::u32BE(std::uint32_t value) { pushBE(out_, value, 4); }
void BinaryWriter::u64BE(std::uint64_t value) { pushBE(out_, value, 8); }

void BinaryWriter::i32BE(std::int32_t value)
{
    pushBE(out_, static_cast<std::uint64_t>(
                     static_cast<std::uint32_t>(value)), 4);
}

void BinaryWriter::i64BE(std::int64_t value)
{
    pushBE(out_, static_cast<std::uint64_t>(value), 8);
}

void BinaryWriter::f32BE(float value)
{
    pushBE(out_, std::bit_cast<std::uint32_t>(value), 4);
}

void BinaryWriter::f64BE(double value)
{
    pushBE(out_, std::bit_cast<std::uint64_t>(value), 8);
}

void BinaryWriter::bytes(std::span<const std::byte> value)
{
    out_.insert(out_.end(), value.begin(), value.end());
}

void BinaryWriter::uuid(const eng::core::Uuid128& value)
{
    const std::array<std::byte, 16> raw = value.toBytesBE();
    out_.insert(out_.end(), raw.begin(), raw.end());
}

// =============================================================================
// BinaryReader
// =============================================================================

eng::core::Result<std::span<const std::byte>> BinaryReader::raw(
    std::size_t count)
{
    if (remaining() < count) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::IOError,
            "BinaryReader: fim do buffer (restam " +
                std::to_string(remaining()) + " bytes, pedidos " +
                std::to_string(count) + ")"});
    }
    const std::span<const std::byte> view(data_ + pos_, count);
    pos_ += count;
    return view;
}

eng::core::Result<std::uint8_t> BinaryReader::u8()
{
    const auto got = raw(1);
    if (got.isError()) {
        return eng::core::makeUnexpected(got.error());
    }
    return static_cast<std::uint8_t>(got.value()[0]);
}

eng::core::Result<std::uint16_t> BinaryReader::u16BE()
{
    const auto got = raw(2);
    if (got.isError()) {
        return eng::core::makeUnexpected(got.error());
    }
    std::uint16_t value = 0;
    for (const std::byte b : got.value()) {
        value = static_cast<std::uint16_t>((value << 8) |
                                           static_cast<std::uint8_t>(b));
    }
    return value;
}

eng::core::Result<std::uint32_t> BinaryReader::u32BE()
{
    const auto got = raw(4);
    if (got.isError()) {
        return eng::core::makeUnexpected(got.error());
    }
    std::uint32_t value = 0;
    for (const std::byte b : got.value()) {
        value = (value << 8) | static_cast<std::uint8_t>(b);
    }
    return value;
}

eng::core::Result<std::uint64_t> BinaryReader::u64BE()
{
    const auto got = raw(8);
    if (got.isError()) {
        return eng::core::makeUnexpected(got.error());
    }
    std::uint64_t value = 0;
    for (const std::byte b : got.value()) {
        value = (value << 8) | static_cast<std::uint8_t>(b);
    }
    return value;
}

eng::core::Result<std::int32_t> BinaryReader::i32BE()
{
    const auto got = u32BE();
    if (got.isError()) {
        return eng::core::makeUnexpected(got.error());
    }
    return static_cast<std::int32_t>(got.value());
}

eng::core::Result<std::int64_t> BinaryReader::i64BE()
{
    const auto got = u64BE();
    if (got.isError()) {
        return eng::core::makeUnexpected(got.error());
    }
    return static_cast<std::int64_t>(got.value());
}

eng::core::Result<float> BinaryReader::f32BE()
{
    const auto got = u32BE();
    if (got.isError()) {
        return eng::core::makeUnexpected(got.error());
    }
    return std::bit_cast<float>(got.value());
}

eng::core::Result<double> BinaryReader::f64BE()
{
    const auto got = u64BE();
    if (got.isError()) {
        return eng::core::makeUnexpected(got.error());
    }
    return std::bit_cast<double>(got.value());
}

eng::core::Result<std::vector<std::byte>> BinaryReader::bytes(
    std::size_t count)
{
    const auto got = raw(count);
    if (got.isError()) {
        return eng::core::makeUnexpected(got.error());
    }
    return std::vector<std::byte>(got.value().begin(), got.value().end());
}

eng::core::Result<eng::core::Uuid128> BinaryReader::uuid()
{
    const auto got = bytes(16);
    if (got.isError()) {
        return eng::core::makeUnexpected(got.error());
    }
    return eng::core::Uuid128::fromBytesBE(got.value());
}

} // namespace eng::serial
