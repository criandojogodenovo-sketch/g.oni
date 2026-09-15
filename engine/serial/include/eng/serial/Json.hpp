#pragma once

/// eng::serial — parse/dump de JSON com limites (FASE 3; ADR-030).
#include <cstddef>
#include <string>
#include <string_view>

#include "eng/core/Result.hpp"
#include "eng/serial/JsonValue.hpp"

namespace eng::serial {

/// Limites de parse (mitigação DoS — missão §2.4/§4.5).
struct JsonLimits {
    std::size_t maxTextBytes = 16u << 20; ///< 16 MiB por padrão
    std::size_t maxDepth = 64;            ///< profundidade máxima de aninhamento
};

/// Parse de JSON puro (RFC 8259). Erros: tamanho excedido, parse inválido
/// (texto vazio, vírgula solta, ...), profundidade excedida — todos
/// ParseError com mensagem clara. NUNCA lança nem aborta.
[[nodiscard]] eng::core::Result<JsonValue> parseJson(
    std::string_view text, const JsonLimits& limits = {});

/// Dump determinístico (chaves ordenadas; UTF-8 inválido vira U+FFFD).
[[nodiscard]] std::string dumpJson(const JsonValue& value);

} // namespace eng::serial
