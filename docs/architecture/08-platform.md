# eng::platform — Fatos do Hospedeiro (FASE 3)

> Quem somos, onde estão as raízes, quais variáveis existem. Fronteira com
> fs e escopo reduzido: [ADR-026](../adr/ADR-026-platform-fs-boundary.md).

## Posição no grafo

```
eng::core, eng::log, eng::fs ──▶ eng::platform
```

platform PRODUZ raízes como `fs::Path` (value type) — nunca faz I/O de
dados. É o ÚNICO módulo autorizado a ramificar por SO (`#ifdef`);
não suportado nesta fase → `#error` claro (Linux apenas).

## API essencial

```cpp
const auto info = eng::platform::PlatformInfo::current();
// info.name/arch/endianness/buildType/addressSanitizer/undefinedSanitizer

const auto paths = eng::platform::PlatformPaths::detect("meu-jogo");
// XDG_DATA_HOME|$HOME/.local/share + /meu-jogo, XDG_CACHE_HOME|…
// tempRoot (TMPDIR|/tmp), executableRoot (/proc/self/exe)

eng::platform::Environment::get("HOME");   // optional<string>
eng::platform::Environment::set("K", "v"); // overwrite opcional
eng::platform::ProcessInfo::currentExecutablePath(); // Result<Path>
```

Fallbacks documentados: sem XDG/HOME → `/tmp/goni-fallback` + `ENG_WARN`;
sem `/proc` → cwd + warn. Nada silencioso.

## O que NÃO tem (por decisão)

Tempo (core cobre), contagem de CPUs (jobs cobre), filesystem (fs),
logging (log), thread pool, display/input — abstração "guarda-chuva" é
dívida (missão §2.2).

## Testes

Endianness validada por sonda de bytes INDEPENDENTE (não tautológica);
raízes não-vazias e coerentes contra o filesystem real via eng::fs;
executableRoot = pai do binário de teste em execução; Environment
round-trip com nome único por PID.
