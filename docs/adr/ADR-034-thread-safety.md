# ADR-034 — Política de thread-safety da FASE 3 (por módulo)

- **Estado:** aceito (FASE 3, missão §2.9)
- **Contexto:** a missão exige declarações EXPLÍCITAS por módulo — nada
  pode ficar implícito, e "não declarar thread-safe" é a regra.

## Decisão

Declaração por módulo (headers documentam o mesmo; nada omitido):

| Módulo | Declaração |
|---|---|
| `eng::fs::FileSystem` (Native/Memory) | **thread-compatible**: instâncias INDEPENDENTES em threads distintas são seguras; UMA instância não é segura para mutação concorrente (sem locks internos). |
| `eng::fs::Path` / `File` | `Path` é value type puro (seguro). `File` tem ownership único — move-only por construção. |
| `eng::platform` | `PlatformInfo`/`PlatformPaths`/`ProcessInfo` pós-construção: dados imutáveis, leitura concorrente segura. `Environment::set/unset` mutam o PROCESSO (semântica setenv) — não usar concorrentemente; `get` é reatrido. |
| `eng::serial` (parse/dump/codec/envelope) | **Funções puras**: seguras em qualquer thread quando os valores de entrada não são mutados concorrentemente. `MigrationRegistry`: init single-threaded, read-only depois. |
| `eng::serial::registerFieldTypeCodec` | Escrita sob lock exclusivo, leitura sob lock compartilhado (mesma política do `TypeRegistry` — ADR-021). |
| `eng::assets::AssetManager` | **SINGLE-THREADED na FASE 3** (missão §2.9). Interface preparada para async na FASE 4 via `loadAsync` — que NÃO existe (R14). |
| `eng::assets::AssetRegistry` | Single-threaded; acesso concorrente à mesma instância requer sincronização externa. |
| `eng::assets::AssetResolver` / loaders | Puros após construção (`const`); seguros entre threads com dados não-mutados. |
| `eng::project` | Inicialização single-threaded; **após construído, read-only** (ProjectConfig/paths são dados). |
| `eng::scene::SceneSerializer` | save/load NÃO são concorrentes sobre a mesma `Scene`. Registro de componentes: init single-threaded; leitura concorrente após registro é segura (mapa imutável na prática). |
| `eng::reflect` (FASE 2, referência) | Registro sob lock exclusivo; leitura concorrente testada (ADR-021). |

## O que NÃO é declarado thread-safe

Nenhum módulo novo declara thread-safety além do listado. Em particular:
NÃO existe nenhuma fila concorrente, cache com lock, ou API async nesta
fase — quem precisar de concorrência compõe com `eng::jobs` (FASE 2) por
conta própria, respeitando as declarações acima.

## Testes

A FASE 3 **não** adiciona testes de concorrência: quase tudo é
single-threaded por declaração, e a missão manda NÃO adicionar TSan nesta
fase (a CI não roda TSan; ADR-023 já cobre `eng::jobs` com harness
dedicado). Quando a FASE 4 introduzir `loadAsync`, os testes de corrida
vêm junto (com TSan no escopo do novo código).
