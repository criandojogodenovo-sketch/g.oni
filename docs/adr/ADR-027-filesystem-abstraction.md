# ADR-027 — Filesystem: Path, FileSystem, MemoryFileSystem

- **Estado:** aceito (FASE 3, missão §2.1/§2.5/§2.11)
- **Contexto:** assets/serial/project precisam de I/O testável sem disco,
  com um único dono da abstração de caminhos.

## Decisão

### Path = wrapper fino de std::filesystem::path

- ENVOLVE `std::filesystem::path` (não substitui; sem re-parsing próprio):
  `join(operator/)`, `parent`, `filename`, `stem`, `extension`,
  `normalized` (lexically_normal), `isAbsolute`, `str()` (forma GENÉRICA
  canônica — o que se persiste), `native()`.
- Ctor com **tag `FromNative`**: elimina a ambiguidade real de conversão
  `const char*`/`std::string` → {string_view, filesystem::path} (ambiguidade
  detectada em compilação durante o desenvolvimento desta fase).
- `valid()` = não-vazio e sem NUL; TODAS as operações de FileSystem rejeitam
  paths inválidos com `InvalidArgument` (checagem centralizada no contrato).
- **`isWithin(root)`** — anti-traversal por COMPONENTE (ambos
  lexically_normal; prefixo por componente, não por bytes). Base do veto a
  `..` no AssetResolver (critério F da missão).

### FileSystem: herança simples, contrato explícito

Base abstrata com métodos virtuais — a missão REJEITOU interface "virtual
pura para substituição em runtime" com mais maquinaria. Contratos:

- `writeAll*` NÃO cria diretórios-pai (paridade exata entre implementações;
  use `mkdirs` antes). Pai ausente → `NotFound`.
- `remove` = semântica de `std::filesystem::remove`: arquivo ou diretório
  **vazio**; `false` quando nada é removido (não é erro).
- `list` devolve ordem **determinística** (crescente por path genérico).
- Erros via `core::Result` + `core::Error` (reuso da FASE 1 — auditado;
  sem `FsError` paralelo).

### NativeFileSystem

`std::filesystem` SEMPRE pelas sobrecargas `std::error_code` + I/O por
`File` (`std::FILE*`, "rb"/"wb"). O runtime compila `-fno-exceptions`
(ADR-004): as formas lançantes de std::filesystem estão proibidas no módulo.

### MemoryFileSystem

- Árvore em `std::map` ordenado por path genérico normalizado → listagem
  determinística sem ordenar por rodada.
- **Paridade semântica** com Native (mesmos contratos, incluindo "writeAll
  não cria pais" e "remove só apaga vazio") — os testes de assets/serial/
  project rodam sobre ele sem tocar disco (flakiness zero em CI).
- Não é thread-safe para acesso concorrente à mesma instância (ADR-034).

### File (RAII)

`std::FILE*` com modo Read/Write binário; move-only; `read`/`write` em
blocos; `size` via fseek/ftell restaurando a posição corrente.

## Alternativas rejeitadas

- **eng::vfs** — abstração de montagem que a FASE 3 não precisa; se um dia
  for necessária, será `eng::fs::VirtualFileSystem` MONTANDO FileSystems.
- **Parser de path próprio** — bugs esperando para acontecer; std::filesystem
  já resolve parsing/normalização lexical.
- **std::fstream** — máscara de exceções e checagens espalhadas; FILE* é
  direto, testado e sem exceções.

## Consequências

- Bugs reais corrigidos durante o desenvolvimento (registrados no commit):
  rename de diretório em MemoryFileSystem montava chaves sem '/' (achado por
  teste de round-trip); expectativas de stem()/parent() alinhadas à
  semântica de std::filesystem.
- Todo o resto da FASE 3 (serial/assets/project) persiste SOMENTE através
  desta abstração — nenhum `std::ifstream` aparece nos módulos novos.
