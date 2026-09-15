# ADR-031 — Versionamento de schema e migrations (infraestrutura)

- **Estado:** aceito (FASE 3, missão §2.4)
- **Contexto:** arquivos persistidos precisam sobreviver a evolução do
  código; a missão exige infraestrutura de migrations SEM migrations ativas
  nesta fase (nenhum schema antigo existe).

## Decisão

### Duas camadas de versão

1. **formatVersion do envelope** (ADR-030): versão do CONTÊINER binário.
   Maior que o suportado → `NotSupported` claro. Interpretar mesmo assim
   seria adivinhação.
2. **payloadVersion (SchemaVersion)**: versão do SCHEMA do conteúdo —
   de responsabilidade do dono do dado (scene, project, registry), que
   decide migrar via `MigrationRegistry` antes de decodificar.

### Migration como passo explícito

```cpp
class Migration {
    virtual SchemaVersion from() const;  // origem do passo
    virtual SchemaVersion to() const;    // destino do passo
    virtual Result<JsonValue> migrate(JsonValue) const;  // transformação
};
```

Cadeia aplicada por `MigrationRegistry::migrateTo(data, current, target)`:

- `current == target` → no-op (dados já na versão do leitor);
- `current > target` → `InvalidArgument` (downgrade não existe);
- passo faltante na cadeia → `NotSupported` com versão exata quebrada;
- passo que ULTRAPASSA o alvo → `NotSupported` (o arquivo é mais novo que
  o leitor — "melhor esforço" silencioso é corruptor disfarçado de
  compatibilidade);
- falha dentro de um passo → `ParseError` com from→to e a causa.

Registro: `from >= to` e `from` duplicado são rejeitados; passos mantidos
ordenados por `from()`.

### Política de campos (schema v1)

- Campo registrado AUSENTE no JSON → erro claro ("campo obrigatório
  ausente") — estrito de propósito: silêncio aqui vira default mágico
  indistinguível de bug;
- Chave DESCONHECIDA no JSON → IGNORADA (evolução aditiva: leitor antigo
  + arquivo novo com campo extra não trava edição);
- adicionar campo obrigatório exige bump de payloadVersion + migration
  que preencha o novo campo — a infraestrutura existe exatamente para
  não precisar de exceção à regra.

### Thread-safety

Registro em inicialização single-threaded; após construído, o registry é
read-only (ADR-034). Diferente do TypeRegistry (registro estático espalhado
por TUs), migrations são poucas e registradas por código dono do schema.

## Consequências

- Nenhuma migration REAL ativa na FASE 3 — a infraestrutura é validada por
  migrations FAKE em teste (cadeia 1→2→4, downgrade, link faltante, passo
  ultrapassante, no-op) — cada propriedade é testável sem mentir sobre o
  estado do motor;
- O primeiro schema real a versionar será o da cena (ADR-033): 
  `formatVersion: 1` no JSON, migrável quando a estrutura evoluir.
