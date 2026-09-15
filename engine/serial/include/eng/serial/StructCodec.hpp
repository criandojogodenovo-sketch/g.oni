#pragma once

/// eng::serial::StructCodec — codifica/decodifica structs REGISTRADAS no
/// eng::reflect (FASE 3; ADR-030/ADR-033).
///
/// - Campos por PropertyInfo (offset + typeName resolvido no TypeRegistry):
///     primitivas ("bool","i8".."u64","f32","f64","string") → valor;
///     structs registradas → objeto aninhado por recursão;
///     enums registradas → NOME do enumerador (nunca índice/valor cru —
///       nomes são estáveis entre builds, ADR-033).
/// - Determinismo: o objeto JSON subjacente ordena chaves (std::map) — a
///   ordem de registro dos campos é irrelevante para o dump.
/// - decode é ESTRITO com campos ausentes (erro claro — schema v1); chaves
///   DESCONHECIDAS no JSON são IGNORADAS (evolução aditiva, ADR-031).
/// - decode escreve por ASSIGNMENT em um objeto JÁ CONSTRUÍDO (não faz
///   placement-new): componentes chegam default-construídos do ECS.
/// - NaN/Inf em f32/f64 são rejeitados no ENCODE (JSON não os representa —
///   falha clara em vez de null silencioso; ADR-030).
#include "eng/core/Result.hpp"
#include "eng/reflect/Reflect.hpp"
#include "eng/serial/JsonValue.hpp"

namespace eng::serial {

/// Objeto → JsonValue (type = TypeInfo registrado do tipo de obj).
[[nodiscard]] eng::core::Result<JsonValue> encodeStruct(
    const void* obj, const eng::reflect::TypeInfo& type);

/// JsonValue → objeto (assign em obj JÁ construído).
[[nodiscard]] eng::core::Result<void> decodeStruct(const JsonValue& value,
                                                   void* obj,
                                                   const eng::reflect::TypeInfo& type);

} // namespace eng::serial
