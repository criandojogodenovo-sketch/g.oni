# eng::serial — JSON, Envelope Binário, Migrations, StructCodec (FASE 3)

> Transformação pura entre valores e as duas formas de persistência.
> Formato e versionamento: [ADR-030](../adr/ADR-030-serialization-format.md)
> · [ADR-031](../adr/ADR-031-serialization-versioning.md).

## Posição no grafo

```
eng::core, eng::reflect ──▶ eng::serial ◀── eng::fs (declarada) + nlohmann/json
```

Funções puras (bytes/strings ⇄ valores) — I/O pertence ao chamador.

## API essencial

```cpp
// JSON com limites (DoS): parse SEM exceções (allow_exceptions=false)
auto value = eng::serial::parseJson(text, {16u << 20, 64}).value();
value.find("campo");        // optional<JsonValue> (cópia segura)
value.at(i);                // elemento de array
eng::serial::dumpJson(value); // determinístico (chaves ordenadas)

// StructCodec — structs do reflect → JSON (ADR-033)
eng::serial::encodeStruct(&obj, *info);   // PropertyInfo offset+typeName
eng::serial::decodeStruct(json, &obj, *info); // assign em obj construído

// Codecs de campo por NOME de tipo (AssetId/SceneEntityId como string)
eng::serial::registerFieldTypeCodec("eng::assets::AssetId", codec);

// Envelope binário mínimo (big-endian, CRC-32 sobre tudo antes do campo)
auto bytes = eng::serial::encodeEnvelope(assetType, payloadVersion, payload);
auto env = eng::serial::decodeEnvelope(bytes).value(); // valida TUDO

// Migrations (infra; NENHUMA ativa — ADR-031)
registry.migrateTo(data, 1, 4);
```

## Invariantes testadas

- parse inválido/vazio/grande/fundo → ParseError claro, sem crash;
- dump determinístico (ordem de inserção irrelevante) e round-trip exato
  de floats (shortest-repr); NaN/Inf rejeitados no encode;
- envelope: CRC errado, magic errado, formatVersion futura
  (NotSupported), truncamento, tamanho inconsistente;
- StructCodec: estrito com campo ausente, ignora chaves desconhecidas,
  enums POR NOME, u64 > i64max preservado (semântica isInteger/isUnsigned
  documentada — armadilha nlohmann neutralizada).

## Concorrência

Funções puras — seguras com dados não-mutados concorrentemente;
`registerFieldTypeCodec`: escrita exclusiva/leitura compartilhada
(política TypeRegistry, ADR-021). MigrationRegistry: init single-threaded.
