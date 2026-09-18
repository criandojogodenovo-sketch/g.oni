# 20 — Build & Export (FASE 12)

> Pipeline de dados do projeto. ADR-050. Design formal pré-implementação:
> `phase12_audit/design.md`.

## Papel no motor

A FASE 12 fecha o ciclo: **projeto editável → pacote de runtime**. O
pipeline é 100% DADOS — nunca recompila o motor, nunca conhece um
projeto específico (missão: engine × dados separados).

```
engine/build (novo)
   deps: core, fs, serial, project, assets, niscript
   NÃO conhece: rhi, android, editor, physics, scene
   (scan de referências = texto JSON + AssetIds — agnóstico)
```

## Pipeline (10 etapas — design §1)

```
1 loadProject     project.goni.json + asset_registry.json
2 loadBuildConfig build.json — paths RELATIVOS (absoluto = erro)
3 buildManifest   enumeração por AssetId + FNV-1a 64 de conteúdo
4 depGraph        entryScenes → scripts embutidos + refs
5 refScan         alcançável × não-usado (WARN, não block)
6 validation      BLOCK: fonte ausente, ids duplicados, script não
                  compila, cena JSON inválida, target/manifest inválidos
7 cooker          1 envelope GONI por asset (payload VERBATIM = SOURCE)
8 cache           chave FNV(cookerVersion ‖ type ‖ conteúdo) — sem
                  timestamps; hit reutiliza, miss cozinha e grava
9 bundle          container GONI único (assetType=1000): manifest +
                  envelopes + meta (id/path/hash/flags)
10 verify+export  decode total + CRC + cruzamento manifest ↔ entradas
                  ANTES de materializar android-arm64 / linux-dev
```

## Decisões estruturais

1. **SOURCE/DERIVED desde o v1** (missão): a classificação é CAMPO do
   formato do bundle; v1 cozinha tudo como SOURCE (payload verbatim) —
   cooks derivados (textura comprimida etc.) chegam sem mudar o formato;
2. **Scripts validados no build, empacotados como fonte**: compilar
   `.nis` é GATE bloqueante (cena embutida ou asset standalone), mas o
   bundle carrega o FONTE — o runtime compila no load (ADR-049:
   serialização de bytecode é futuro; uma única fonte de verdade);
3. **Cache conteúdo-endereçado**: mudou conteúdo/versão/tipo ⇒ outra
   chave — invalidação automática; apagar o cache é sempre seguro
   (`forceCook` = clean build);
4. **Determinismo testado**: mesma árvore ⇒ mesmos bytes do manifest e
   do bundle (2 builds limpos idênticos, byte-a-byte);
5. **Não-usado = WARN**: o report lista `unusedAssets`; empacota mesmo
   assim (cenas futuras podem referenciar; a missão pede DETECTAR).

## Alvos

- **android-arm64** (prioritário): `<out>/<nome>.goni` + `INSTALL.md`
  documentando o caminho no APK existente (`filesDir/projects/<id>/`)
  — o runtime arm64-v8a já embute engine+editor; dados são INJETADOS;
- **linux-dev**: bundle + `README.md` com layout e como validar.

**Honesto**: o LOADER do bundle na Activity do runtime (browsing de
projetos no APK) é FUTURO declarado — o formato, a validação e os
alvos existem e são testados; assinatura de APK e splits estão fora do
escopo v1.

## Testes (23 casos / 232 asserções)

Config (ausente/target/versão/entrada/path absoluto), projeto mínimo,
determinismo byte-a-byte, ausente-BLOCK, não-usado-WARN, duplicado-BLOCK,
referência-por-id (grafo), scripts válidos/quebrados (embutido e
standalone) BLOCK, cena corrompida BLOCK, cache hit/miss/invalidação
por conteúdo/por versão/forceCook, E2E export dos dois alvos com
verificação independente (decode + manifest ↔ entradas + contentHash +
payload == fonte) e bundle corrompido → verify falha.
