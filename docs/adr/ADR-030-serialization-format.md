# ADR-030 — Formato de serialização: JSON puro + envelope binário mínimo

- **Estado:** aceito (FASE 3, missão §2.4)
- **Contexto:** assets/cenas precisam de formato humano editável e de um
  formato binário para cache; a proposta original citava JSON5/CBOR/
  MessagePack/FlatBuffers como candidatos.

## Decisão

### JSON puro (RFC 8259) — sem JSON5

JSON5 (comentários, trailing commas, aspas simples) foi REJEITADO: ganho
pequeno para o editor, custo de dependência extra e superfície de bug. Se
comentários forem necessários, o editor usa campos especiais ou arquivos
separados — decisão registrada na auditoria da missão.

### nlohmann/json v3.11.3 (MIT) via FetchContent

Padrão JÁ existente do repositório (EngineDependencies.cmake, tag pinada,
proteção de BUILD_TESTING). Parser JSON próprio foi proibido pela missão —
correto: é bug esperando para acontecer.

**Integração com -fno-exceptions (ADR-004):** o uso de nlohmann fica
confinado às vias que não lançam, por construção do wrapper `JsonValue`:

- `parseJson` usa `json::parse(..., allow_exceptions=false)` +
  `is_discarded()` — erro de sintaxe vira `Result` com `ParseError`;
- leitura SEMPRE pré-checada (`isString`/`isNumber`/... antes de `get`) —
  a fachada não expõe `get<T>` cego;
- `dump` com `error_handler_t::replace`: UTF-8 inválido vira U+FFFD
  (determinístico) em vez de abortar o runtime. Entrada VÁLIDA produz saída
  idêntica — o replace só age fora do contrato RFC 8259;
- `JSON_ImplicitConversions=OFF` no build da dependência (conversões
  implícitas json→T são armadilha);
- includes da dependência marcados SYSTEM — warnings do projeto (ADR-020)
  aplicam-se ao NOSSO código; dependências compilam com flags nativos
  (política já registrada em EngineWarnings.cmake).

### Semântica do wrapper (documentada no header)

- `isInteger()` é ESTRITAMENTE com sinal; `isUnsigned()` cobre o sem sinal
  (nlohmann parseia inteiros positivos como unsigned — armadilha encontrada
  e neutralizada durante o desenvolvimento; sem isso, u64 > i64max era
  lido como negativo).
- `at()`/`find()` retornam POR VALOR (cópia) — mantém o wrapper livre de
  casts entre layouts de classes (UB técnico); o codec interno lê via
  `raw()` sem cópia.

### Determinismo

- Objetos são `std::map` internamente → chaves ordenadas no dump,
  independente da ordem de inserção/construção (testado);
- floats saem na forma mais curta que preserva o valor (round-trip exato
  f32→double→texto→double→f32);
- NaN/Inf são REJEITADOS no encode com erro claro (JSON não os representa;
  null silencioso seria corrupção).

### Limites de parse (mitigação DoS)

`JsonLimits{maxTextBytes=16 MiB, maxDepth=64}`: tamanho checado antes do
parse; profundidade verificada por varredura iterativa (sem recursão) no
DOM resultante. Ambos excedidos → `ParseError` claro, sem crash.

### Envelope binário mínimo

```
[magic 'G','O','N','I' 4B][formatVersion u32][assetType u32]
[payloadVersion u32][payloadSize u64][payload][crc32 u32]
```

Sem compressão, sem criptografia, sem TLV — "Só. Sem fancy." (missão
§2.4). Tudo big-endian; CRC-32/ISO-HDLC (vetor "123456789" → 0xCBF43926
validado em teste) cobre TUDO antes do próprio campo. Validações na
leitura: magic, formatVersion (futuro → `NotSupported` claro), consistência
payloadSize×tamanho total, CRC. Nunca aborta.

## Alternativas rejeitadas

- **RapidJSON**: só se nlohmann não compilasse com as flags atuais —
  compilou limpo (SYSTEM includes, zero warnings).
- **CBOR/MessagePack/FlatBuffers**: over-engineering sem evidência de
  necessidade; compression entra quando houver dados empíricos.
- **Alias puro `JsonValue = nlohmann::json`**: permitiria os caminhos que
  lançam — o wrapper existe para torná-los inacessíveis por engano.

## Consequências

- Compile-time: headers nlohmann propagam PUBLIC para consumidores de
  eng::serial (custo aceito nesta fase; pimpl seria alocação por valor).
- StructCodec (reflect-driven) fica neste módulo: primitivas por nome
  canônico, structs por recursão, enums por NOME de enumerador (ADR-033).
