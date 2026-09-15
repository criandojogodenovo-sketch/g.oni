# ADR-028 — Identidade de asset: UUIDv4 de 128 bits

- **Estado:** aceito (FASE 3, missão §2.3)
- **Contexto:** a proposta original listava UUID/hash/content-hash/path-hash
  "para analisar trade-offs" — sem decidir. A FASE 3 exige decisão única.

## Decisão

### AssetId = UUIDv4 {hi, lo} — tipo forte sobre `core::Uuid128`

- **16 bytes POD**, comparável, hashable, ordem total (hi,lo) — ordenação
  determinística de registros e arquivos.
- **Por que não path-hash:** renomear/mover o asset quebraria TODAS as
  referências — o nome é metadado, não identidade.
- **Por que não content-hash como identidade:** mudar 1 pixel de textura
  mudaria o id e todas as referências. Conteúdo é versão; identidade é
  entidade. Content-hash fica como METADADO futuro (`AssetMeta::
  contentHash`, declarado, jamais calculado na FASE 3) para dedup/cache.
- **Por que não hash truncado:** colisão silenciosa é inaceitável numa
  engine de produção; 122 bits aleatórios do v4 dão margem de segurança
  contra birthday attack em qualquer escala humana de projeto.

### Gerador próprio (~80 linhas em core::Uuid128)

`std::random_device` semeia `std::mt19937_64` `thread_local`. Sem stduuid,
libuuid ou Boost — código pequeno, auditável, testado (100k gerações sem
colisão — teste REAL de inserção em conjunto, não argumento probabilístico).

**Localização (desvio D1 da auditoria):** a missão sugeria o gerador em
`eng::assets`, mas TRÊS módulos em camadas distintas precisam de UUIDv4
(assets:AssetId, project:ProjectId, scene:SceneEntityId) e a regra "ninguém
depende de scene" + o layering proíbem `scene → assets`. O ancestral comum
é `core` — extensão ADITIVA (arquivos novos; semântica de nada existente
alterada). Alternativa rejeitada e registrada.

### Formas canônicas

- **Texto (JSON):** `xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx` — minúsculas,
  hífens, sem chaves, sem `urn:`. Parser ESTRITO: rejeita maiúsculas,
  chaves, v≠4, variante ≠ RFC 4122, hex inválido, tamanho errado.
- **Binário (envelope):** 16 bytes BIG-ENDIAN, mesma validação de
  versão/variante.

### Renomeação/movimentação

O AssetId é gravado no ASSET SOURCE e no registry; `AssetRegistry` mantém
id → sourcePath. Renomear arquivo = `upsert` com novo path (testado:
referências por AssetId continuam resolvendo). O id JAMAIS muda.

## Consequências

- `AssetId`/`ProjectId`/`SceneEntityId` são tipos fortes DISTINTOS sobre o
  mesmo valor — impossível trocar um pelo outro por acidente (missão §2.7).
- Ordem total (hi,lo) é a ordem canônica de serialização de coleções
  (registry, entidades de cena — ADR-033).
