# ADR-032 — Estrutura de projeto: apenas caminhos relativos

- **Estado:** aceito (FASE 3, missão §2.6)
- **Contexto:** um projeto de jogo precisa ser movível (clone, branch,
  CI) sem quebrar; a proposta original assumia estrutura rígida demais.

## Decisão

### Layout ESPERADO, não imposto

```
meu-jogo/
├── project.goni.json     # raiz lógica (formatVersion 1)
├── asset_registry.json   # catálogo (ADR-029) — path vem do config
├── assets/               # raiz convencional de assets (sourcePaths)
│   └── scenes/           # sceneRoots do config (0..n)
├── cache/                # derivado, regenerável (nada cookado na FASE 3)
└── build/                # saídas de build
```

`docs/project-layout.md` documenta um exemplo mínimo. A engine impõe
apenas: o ARQUIVO existe e os paths dentro dele são relativos.

### Regra dura: NENHUM absoluto persistido

- `ProjectConfig` carrega `assetRegistryPath` e `sceneRoots[]`
  RELATIVOS ao diretório do `project.goni.json`;
- o PARSE rejeita absoluto com `InvalidArgument` ("ABSOLUTO é proibido") —
  validação ativa, não convenção (testado para os dois campos);
- `ProjectPaths` COMPUTA os absolutos: `assetsRoot/cacheRoot/buildRoot`
  convencionais + `resolve(rel)` genérico + `sceneRoots(config)`;
- consequência (critério E, testado): o MESMO texto de projeto resolve
  raízes diferentes quando o arquivo muda de diretório — mover/renomear o
  projeto não invalida nada.

### Campos e validações do project.goni.json

`formatVersion` (maior que a suportada → `NotSupported`), `projectId`
(UUIDv4 canônico — ADR-028), `name` (não-vazio), `engineVersion`
(`MAJOR.MINOR.PATCH` via `core::Version::parse`), `assetRegistryPath`
(relativo), `sceneRoots` (array de relativos, opcional). JSON
determinístico (chaves ordenadas — ADR-030); round-trip byte-estável
testado.

### Defaults de sistema via platform

`ProjectPaths::systemDefaults(appName)` resolve o cache de usuário via
`eng::platform::PlatformPaths` (XDG) — consumo REAL da aresta platform
(§5.1), útil quando o projeto não é "portable".

### Papel do módulo

Descrever e LOCALIZAR o projeto. O pipeline projeto→registry→assets é
montado pelo CHAMADOR (teste de integração em tests/): `eng::project`
não orquestra cargas — as arestas assets/log ficam declaradas para a
composição externa (precedente ecs→reflect da FASE 2).

## Alternativas rejeitadas

- **Estrutura rígida imposta** (build/export dentro da engine): acoplaria
  projeto a toolchain — FASE 8+;
- **Paths absolutos persistidos**: quebram clone/mover/CI — a regra dura
  existe exatamente para isto;
- **Múltiplos arquivos de config**: um arquivo, uma verdade, parse único.
