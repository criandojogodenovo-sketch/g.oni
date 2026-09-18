# ADR-050 — Build & Export: pipeline de dados com envelope GONI

- **Status**: ACEITO (FASE 12)
- **Data**: 2026-09-18
- **Contexto**: a FASE 12 exige transformar um projeto editável em um
  pacote de runtime determinístico e verificável, priorizando o alvo
  Android arm64-v8a (APK já existente) sem recompilar o motor.

## Decisão

1. **Pipeline de DADOS puro** (`engine/build`): projeto → manifest →
   grafo/scan → validação → cook → cache → bundle verificado → export.
   O módulo depende apenas de core/fs/serial/project/assets/niscript —
   nunca de rhi/android/editor; o scan de referências é TEXTO JSON +
   AssetIds do registro (agnóstico de componente).
2. **Envelope GONI como unidade de cook** (reuso — ADR-030): cada asset
   vira um envelope (magic/type/payloadVersion/CRC-32); o BUNDLE é um
   envelope externo (assetType=1000) contendo manifest + envelopes +
   meta por entrada (id/path/contentHash/flags).
3. **Classificação SOURCE/DERIVED no formato desde o v1**: hoje todo
   cook é SOURCE (payload verbatim); o campo existe para cooks derivados
   futuros não quebrarem o formato (compatibilidade).
4. **Scripts: validação por COMPILAÇÃO no build (bloqueante),
   empacotamento como FONTE.** Racional: a serialização de bytecode é
   futuro declarado do ADR-049; compilar no load é barato e mantém uma
   única fonte de verdade; um script que não compila NUNCA chega ao
   pacote.
5. **Cache conteúdo-endereçado**: chave = FNV-1a 64 de
   `cookerVersion ‖ assetType ‖ bytes do fonte`. Invalidação automática
   por construção; nenhum timestamp; `forceCook` = clean build; o cache
   é artefato puro (apagar é sempre seguro).
6. **Paths RELATIVOS obrigatórios** na `build.json` (targets,
   entryScenes, cacheDir, outDir): path absoluto = erro — o estado do
   build nunca escapa do projeto.
7. **Não-usado = WARN** (detecta, não pune): o report lista ids não
   alcançáveis; o build segue — a missão pede a DETECÇÃO.

## Alternativas consideradas

- **Empacotar direto em JSON único**: sem CRC por asset, sem tipos,
  sem binário estável — o envelope GONI já existia e é testado;
- **Cook DERIVED já em v1** (texturas comprimidas etc.): sem loaders de
  textura implementados (AssetType 100+ é RESERVADO — FASE 3), seria
  especulação; o marcador no formato resolve a evolução;
- **Cache com timestamps/mtimes**: não determinístico entre
  filesystems e frágil para sincronização — conteúdo-endereçado é
  superior e simples;
- **Zip/tar como container**: dependência de codec, complexidade de
  índice e CRC redundante ao que o envelope já dá.

## Consequências

- (+) Determinismo byte-a-byte testado (manifest e bundle);
- (+) Verificação em DUAS camadas (CRC do envelope por asset + CRC do
  envelope externo + cruzamento manifest ↔ entradas + contentHash);
- (+) Invalidação de cache sem estado e sem relógio;
- (−) Payload verbatim = bundles maiores (sem compressão — coerente com
  ADR-030 "sem compression, sem fancy");
- (−) Loader no runtime Android ainda não existe (futuro declarado) —
  o pipeline entrega e valida o pacote, o consumidor vem depois.

## Referências

- Design formal: `phase12_audit/design.md` (pipeline §1, formatos §2–9)
- Especificação: `docs/architecture/20-build-export.md`
- Envelope: `docs/adr/ADR-030-serial-envelope.md` (CRC-32 exposto
  "para testes e usos futuros de cache" — este ADR é esse uso)
