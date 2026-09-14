# eng::reflect — Metadados de Tipos (FASE 2)

> Módulo de reflexão declarativa por registro explícito. Estratégia completa e
> decisões de arquitetura: [ADR-021](../adr/ADR-021-reflection-strategy.md).

## Posição no grafo

```
eng::core ──▶ eng::reflect
```

`eng::reflect` situa-se acima de `eng::core` na camada de fundações estendidas
(missão §B.0). Nenhum símbolo de `eng::core` é consumido nesta fase — o link
declara a camada para que integrações futuras (ex.: `Result` em APIs de
registro) não requeiram re-arranjo do grafo.

## API essencial

```cpp
// Registro por macro (escopo de namespace):
ENG_REFLECT_BEGIN(Material)
    ENG_REFLECT_FIELD(roughness)          // tipo via PrimitiveName<T>
    ENG_REFLECT_FIELD_AS(albedo, "eng::math::Vec3")
ENG_REFLECT_END()

ENG_REFLECT_ENUM_BEGIN(Color)
    ENG_REFLECT_ENUM_VALUE(Red)
    ENG_REFLECT_ENUM_VALUE(Green)
ENG_REFLECT_ENUM_END()

// Consulta (nunca aborta; ausente → nullptr):
auto* info = eng::reflect::TypeRegistry::global().find("Material");
// info->id / kind / size / alignment / properties[] / enumerators[]
```

`PropertyInfo` carrega nome, offset, `typeName` e `typeId` (resolvido quando o
tipo do campo já estava registrado; senão 0, consultável por `typeName`).
`TypeId` é FNV-1a 64 do nome canônico — determinístico entre TUs e execuções.

## Concorrência

Escrita sob lock exclusivo; leitura sob lock compartilhado (leituras
concorrentes testadas). Registro esperado em startup estático; `clear()` é
restrito a testes (invalida ponteiros `TypeInfo*`).

## Testes (11 casos)

Tipos embutidos; struct com propriedades (offsets verificados contra
`offsetof`); lookup inexistente → nullptr; lookup por id; enums escopado e
não-escopado com subjacente; idempotência (primeiro vence); registro em
runtime + lifetime além do escopo (deep copy); leitura concorrente
(8 threads × 20k); contagem; `clear()` como último teste do binário.
