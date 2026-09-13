#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>

namespace eng::core {

/// ADR-001: std::span é a "view" contígua canônica do motor.
template <typename T>
using Span = std::span<T>;

/// View de bytes somente-leitura.
using ByteSpan = Span<const std::byte>;

/// View de bytes gravável.
using MutableByteSpan = Span<std::byte>;

/// Bytes de uma região de memória (leitura bruta, p.ex. serialização).
[[nodiscard]] constexpr ByteSpan asBytes(const void* data, std::size_t size) noexcept {
    return {static_cast<const std::byte*>(data), size};
}

/// Bytes de um objeto trivially-copyable.
template <typename T>
[[nodiscard]] constexpr ByteSpan asBytes(const T& value) noexcept {
    static_assert(std::is_trivially_copyable_v<T>, "asBytes exige tipo trivially-copyable");
    return {reinterpret_cast<const std::byte*>(std::addressof(value)), sizeof(T)};
}

/// Span de chars a partir de string_view (sem NUL terminador).
[[nodiscard]] constexpr Span<const char> asSpan(std::string_view text) noexcept {
    return {text.data(), text.size()};
}

} // namespace eng::core
