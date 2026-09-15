#pragma once

/// eng::core::Uuid128 — identidade UUIDv4 de 128 bits (FASE 3; ADR-028).
///
/// Extensão ADITIVA de eng::core (desvio D1 de docs/phase3_audit.md): a
/// missão original colocava o gerador em eng::assets, mas TRÊS módulos em
/// camadas distintas precisam dele (assets:AssetId, project:ProjectId,
/// scene:SceneEntityId) e a regra "ninguém depende de scene" força um
/// ancestral comum — core é o único. Nenhum arquivo existente de FASE 1
/// teve semântica alterada; este header é novo.
///
/// - POD de 16 bytes {hi, lo}; comparável, hashable, trivialmente copiável.
/// - generate(): UUIDv4 com std::random_device + std::mt19937_64 (gerador
///   próprio auditável — SEM dependência externa; 122 bits aleatórios).
/// - Forma canônica ESTREITA: "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"
///   (minúsculas, hífens, sem chaves, sem urn:). fromString rejeita TUDO
///   que não for exatamente isso (incluindo maiúsculas, chaves, v≠4,
///   variante inválida).
/// - Binário: 16 bytes BIG-ENDIAN (toBytesBE/fromBytesBE).
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include "eng/core/Result.hpp"

namespace eng::core {

struct Uuid128 {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;

    /// UUID nulo (todos os bits em zero)?
    [[nodiscard]] bool isNil() const noexcept { return hi == 0 && lo == 0; }

    /// Forma canônica dos bits como estão (8-4-4-4-12, minúsculas) —
    /// formata qualquer valor; validação de v4/variante é do fromString.
    [[nodiscard]] std::string toString() const;

    /// Parse ESTRITO da forma canônica (v4 + variante RFC 4122). Erros:
    /// ParseError com mensagem precisa do motivo.
    [[nodiscard]] static Result<Uuid128> fromString(std::string_view text);

    /// 16 bytes big-endian.
    [[nodiscard]] std::array<std::byte, 16> toBytesBE() const noexcept;

    /// Leitura de 16 bytes big-endian (valida v4 + variante, como fromString).
    [[nodiscard]] static Result<Uuid128> fromBytesBE(
        std::span<const std::byte> bytes);

    /// UUIDv4 novo (random_device semeia mt19937_64 por thread).
    [[nodiscard]] static Uuid128 generate();

    [[nodiscard]] friend bool operator==(const Uuid128&,
                                         const Uuid128&) noexcept = default;

    /// Ordem total determinística (por hi, depois lo) — ordenação estável
    /// de registros e arquivos.
    [[nodiscard]] friend std::strong_ordering operator<=>(
        const Uuid128& a, const Uuid128& b) noexcept
    {
        if (const auto c = a.hi <=> b.hi; c != 0) {
            return c;
        }
        return a.lo <=> b.lo;
    }
};

static_assert(sizeof(Uuid128) == 16, "Uuid128 deve ser exatamente 16 bytes");
static_assert(std::is_trivially_copyable_v<Uuid128>,
              "Uuid128 deve ser trivialmente copiável (serialização direta)");

} // namespace eng::core

/// Hash de Uuid128 — habilita uso em unordered containers.
template<>
struct std::hash<eng::core::Uuid128> {
    [[nodiscard]] std::size_t operator()(
        const eng::core::Uuid128& u) const noexcept
    {
        const std::uint64_t mixed = u.hi ^ (u.lo * 0x9E3779B97F4A7C15ull);
        return static_cast<std::size_t>(mixed);
    }
};
