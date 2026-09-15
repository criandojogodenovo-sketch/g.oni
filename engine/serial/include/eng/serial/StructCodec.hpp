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
///
/// Codec de campo por NOME de tipo (extensão ADR-033): tipos-valor com
/// forma textual própria (UUIDs de identidade — AssetId, SceneEntityId)
/// registram {encode, decode} por typeName; o codec SOMBREIA a resolução
/// por reflect para esse nome. É o mecanismo que permite "AssetId em
/// componentes como string UUID" SEM aresta scene→assets (D1/D4): quem
/// conhece o tipo registra o codec no SEU módulo.
#include <string_view>

#include "eng/core/Result.hpp"
#include "eng/reflect/Reflect.hpp"
#include "eng/serial/JsonValue.hpp"

namespace eng::serial {

/// Par encode/decode para um tipo de campo (member aponta para o campo
/// bruto, por offset — o codec conhece o tipo concreto).
struct FieldTypeCodec {
    eng::core::Result<JsonValue> (*encode)(const void* member) = nullptr;
    eng::core::Result<void> (*decode)(const JsonValue& field,
                                      void* member) = nullptr;
};

/// Registra codec para o typeName (idempotente: re-registro substitui).
/// Thread-safe (escrita sob lock exclusivo — como TypeRegistry, ADR-021).
void registerFieldTypeCodec(std::string_view typeName, FieldTypeCodec codec);

/// Consulta por typeName (nullptr se ausente; leitura concorrente segura).
[[nodiscard]] const FieldTypeCodec* findFieldTypeCodec(
    std::string_view typeName);

/// Objeto → JsonValue (type = TypeInfo registrado do tipo de obj).
[[nodiscard]] eng::core::Result<JsonValue> encodeStruct(
    const void* obj, const eng::reflect::TypeInfo& type);

/// JsonValue → objeto (assign em obj JÁ construído).
[[nodiscard]] eng::core::Result<void> decodeStruct(const JsonValue& value,
                                                   void* obj,
                                                   const eng::reflect::TypeInfo& type);

} // namespace eng::serial
