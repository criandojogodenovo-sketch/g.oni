# eng::project — Descrição e Localização de Projeto (FASE 3)

> Apenas caminhos relativos; raízes computadas do diretório do arquivo.
> Decisões: [ADR-032](../adr/ADR-032-project-structure.md) · layout
> esperado: [../project-layout.md](../project-layout.md).

## Posição no grafo

```
eng::core, eng::fs, eng::serial, eng::platform ──▶ eng::project
                                                   ◀── eng::assets, eng::log (declaradas)
```

O pipeline projeto→registry→assets é montado pelo CHAMADOR (tests/ de
integração demonstra a composição) — project não orquestra cargas.

## API essencial

```cpp
auto file = eng::project::ProjectFile::readFrom(fs,
    eng::fs::Path{"/jogo/project.goni.json"}).value();

file.config.projectId;            // ProjectId (UUIDv4 forte)
file.config.engineVersion;        // core::Version semver
file.config.assetRegistryPath;    // RELATIVO (absoluto → rejeitado)
file.config.sceneRoots;           // relativos

file.paths().assetsRoot();        // /jogo/assets (convencional)
file.paths().cacheRoot();         // /jogo/cache
file.paths().resolve(rel);        // genérico
file.paths().sceneRoots(config);  // todos resolvidos

eng::project::ProjectPaths::systemDefaults("app"); // XDG via platform
```

## Invariantes testadas

- NENHUM string absoluta persistida (parse rejeita com InvalidArgument —
  assetRegistryPath E sceneRoots);
- mover o arquivo de diretório resolve raízes novas sem invalidar nada
  (mesmo texto, dois lugares — critério E);
- formatVersion futura → NotSupported; JSON corrompido/sem campos →
  ParseError claro; engineVersion semver validada;
- round-trip byte-estável (chaves ordenadas).

## Concorrência

Inicialização single-threaded; após construído, read-only (ADR-034).
