# ADR-029 — Ciclo de vida de assets e separação source/cache/runtime

- **Estado:** aceito (FASE 3, missão §2.5/§2.12/§2.10)
- **Contexto:** assets precisam de catálogo persistente, resolução segura
  e cache runtime — sem GPU, importadores reais ou async (proibidos nesta
  fase).

## Decisão

### Componentes

- **AssetType** — `Scene/Prefab/Json` implementados; `Texture/Mesh/
  Material/Shader/Audio/Script` RESERVADOS (valores com folga, nomes
  estáveis, SEM loader — declarado, não implementado). Carregar tipo
  reservado → `NotSupported` claro (testado).
- **AssetMeta** — id, type, sourcePath RELATIVO, size opcional (informativo),
  contentHash opcional (declarado, nunca calculado — ADR-028).
- **AssetRegistry** — vetor ordenado por AssetId (invariante do upsert);
  `asset_registry.json` com `formatVersion` e entradas ordenadas → bytes
  determinísticos (round-trip testado). Upsert substitui (renomeação);
  id nulo e remove-ausente são erros claros.
- **AssetResolver** — id → registry → **validações** → path absoluto dentro
  da raiz → bytes via FileSystem:
  1. sourcePath relativo (absoluto → `InvalidArgument`);
  2. `(root / sourcePath).normalized()` CONTIDO em root por componente
     (`fs::Path::isWithin`) — `../../etc/passwd` rejeitado (critério F,
     testado com registry forjado).
- **AssetManager** — cache single-threaded:
  - `load<T>(id) → Result<AssetHandle<T>>`: cache → registry → loader →
    bytes → decode. Ausente/sem loader/tipo sem suporte/parse → Result de
    erro (NUNCA throw, NUNCA handle inválido silencioso);
  - `getLoaded<T>(id) → optional<AssetHandle<T>>`: nullopt se não está no
    cache OU se T difere do tipo carregado — **guarda de tipo sem RTTI**
    pela chave TypeTag do loader (mesmo padrão de ecs/events);
  - `unload(id)`: remove do cache; handles vivos seguram os dados
    (shared_ptr — sem dangling silencioso; testado).
- **IAssetLoaderFor<T> + LoaderEntry type-erased** — loaders por tipo C++;
    o mapa heterogêneo guarda `shared_ptr<void>` + ponteiros-de-função que
    reencaminham com o cast garantido pela chave (sem RTTI, sem UB).
- **JsonAssetLoader** — serve Scene/Prefab/Json devolvendo `serial::
  JsonValue` parseado com limites. A interpretação ESTRUTURAL de cena é do
  `SceneSerializer` em eng::scene (desvio D4: assets não depende de scene —
  §5.1); a composição fim-a-fime é exercitada no teste de integração.

### source / cache / runtime — conceitual nesta fase

- **source** = os arquivos editáveis (JSON) referenciados pelo registry;
- **cache** = diretório derivado do projeto (ProjectPaths.cacheRoot);
  na FASE 3 nada é COOKED para ele — o envelope binário (ADR-030) é o
  formato FUTURO do cache; existe, é testado, e não é escrito por nenhum
  pipeline ainda;
- **runtime** = dados em memória (cache do AssetManager + handles).
  NÃO é diretório (missão §3 item 8).

### Async fica para a FASE 4 — sem fingir

- NENHUMA função async existe (R14): sem futures, sem callbacks, sem jobs;
- `eng::assets` NÃO depende de `eng::jobs` (aresta proibida);
- `events` é aresta DECLARADA sem consumo (a missão §5.1 fixa; o consumo
  entra com `loadAsync` na FASE 4 — precedente ecs→reflect da FASE 2).

### Thread-safety (ADR-034)

AssetManager/AssetRegistry/AssetResolver: single-threaded na FASE 3
(documentado nos headers); loaders são puros (const) e seguros entre
threads se os dados de entrada não são mutados.

## Consequências

- Editor pode carregar cena com referência quebrada (id ausente no
  registry): o carregamento do ARQUIVO falha claro, mas quem reporta
  "qual referência quebrou" é o consumidor de cena via eng::log
  (ADR-033) — separação de responsabilidades deliberada.
- Round-trips testados: registry JSON determinístico; renomeação preserva
  referências; unload preserva handles vivos.
