# ADR-026 — Fronteira eng::platform / eng::fs

- **Estado:** aceito (FASE 3, missão §2.1/§2.2)
- **Contexto:** a proposta original colocava caminhos e diretórios em ambos
  os módulos — dependência circular em potencial e ambiguidade de ownership.

## Decisão

### fs é o único dono de path e I/O

`eng::fs` define `Path`, `File`, `FileSystem` (base abstrata),
`NativeFileSystem` e `MemoryFileSystem` (ADR-027). Nenhum outro módulo
define tipos de caminho próprios.

### platform só PRODUZ raízes; nunca consome I/O

`eng::platform` contém exatamente: `PlatformInfo` (nome/arquitetura/
endianness/build/sanitizers), `PlatformPaths` (raízes userData/cache/temp/
executable **como `fs::Path` por valor**), `Environment` (get/set/unset) e
`ProcessInfo::currentExecutablePath`. Consequentemente a aresta é
**platform → fs** (e core/log); a direção INVERSA é proibida por build
review e pelo grafo de `docs/architecture/00-overview.md`.

### Autorização de #ifdef

`eng::platform` é o ÚNICO módulo do motor autorizado a ramificar por SO em
tempo de compilação (é a razão dele existir). SO não suportado na FASE 3 →
`#error` claro (Linux apenas). Chamadas JNI/AAssetManager ficarão em
`eng::platform` (host) ou `eng::fs_android` (FASE 6/7) — nunca em `eng::fs`
genérico.

### Escopo deliberadamente pequeno

Nada de tempo (core cobre), contagem de CPUs (jobs cobre), filesystem,
logging, thread pool, display/input. Abstração "guarda-chuva" é dívida.

### Fallbacks documentados

Sem `XDG_*`/`HOME` (container mínimo): raízes sob `/tmp/goni-fallback` +
`ENG_WARN`. Sem `/proc/self/exe`: executableRoot = cwd + warn. Falhas são
sempre DETECTÁVEIS nos logs — nunca silenciosas.

## Consequências

- Grafo permanece acíclico: `fs → core,log`; `platform → core,log,fs`.
- Testes de platform validam raízes não-vazias contra o filesystem real
  (`eng::fs::NativeFileSystem`) — a composição das duas camadas é exercitada.
- Desvio da missão original registrado em `docs/phase3_audit.md` (a proposta
  de plataforma "guarda-chuva" foi reduzida conforme §2.2 da auditoria).
