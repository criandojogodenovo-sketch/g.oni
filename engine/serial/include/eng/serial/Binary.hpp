#pragma once

/// eng::serial — primitivas binárias big-endian (FASE 3; ADR-030).
///
/// Writer acumula em buffer próprio do chamador; Reader valida limites em
/// toda leitura (EOF → Result de erro, nunca leitura fora do buffer).
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "eng/core/Result.hpp"
#include "eng/core/Uuid.hpp"

namespace eng::serial {

class BinaryWriter final {
public:
    /// Escreve no buffer do chamador (append).
    explicit BinaryWriter(std::vector<std::byte>& out) noexcept : out_(out) {}

    void u8(std::uint8_t value);
    void u16BE(std::uint16_t value);
    void u32BE(std::uint32_t value);
    void u64BE(std::uint64_t value);
    void i32BE(std::int32_t value);
    void i64BE(std::int64_t value);
    void f32BE(float value);
    void f64BE(double value);
    void bytes(std::span<const std::byte> value);
    void uuid(const eng::core::Uuid128& value);

    [[nodiscard]] std::vector<std::byte>& buffer() noexcept { return out_; }

private:
    std::vector<std::byte>& out_;
};

class BinaryReader final {
public:
    BinaryReader(const std::byte* data, std::size_t size) noexcept
        : data_(data), size_(size)
    {
    }
    explicit BinaryReader(std::span<const std::byte> data) noexcept
        : data_(data.data()), size_(data.size())
    {
    }

    [[nodiscard]] eng::core::Result<std::uint8_t> u8();
    [[nodiscard]] eng::core::Result<std::uint16_t> u16BE();
    [[nodiscard]] eng::core::Result<std::uint32_t> u32BE();
    [[nodiscard]] eng::core::Result<std::uint64_t> u64BE();
    [[nodiscard]] eng::core::Result<std::int32_t> i32BE();
    [[nodiscard]] eng::core::Result<std::int64_t> i64BE();
    [[nodiscard]] eng::core::Result<float> f32BE();
    [[nodiscard]] eng::core::Result<double> f64BE();
    /// Lê `count` bytes exatos (EOF → IOError claro).
    [[nodiscard]] eng::core::Result<std::vector<std::byte>> bytes(
        std::size_t count);
    [[nodiscard]] eng::core::Result<eng::core::Uuid128> uuid();

    [[nodiscard]] bool atEnd() const noexcept { return pos_ >= size_; }
    [[nodiscard]] std::size_t remaining() const noexcept
    {
        return size_ - pos_;
    }

private:
    /// Lê `count` bytes crus sem consumir validação de tipo.
    [[nodiscard]] eng::core::Result<std::span<const std::byte>> raw(
        std::size_t count);

    const std::byte* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t pos_ = 0;
};

static_assert(std::numeric_limits<float>::is_iec559,
              "f32BE exige IEEE 754 binary32");
static_assert(std::numeric_limits<double>::is_iec559,
              "f64BE exige IEEE 754 binary64");

} // namespace eng::serial
