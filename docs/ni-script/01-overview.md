# NI-Script — 01: Visão geral

> FASE 11 · ADR-049 · design: `phase11_audit/design.md`

NI-Script é a linguagem de script **própria** do G.oni — arquivos `.nis`,
compilados UMA vez para bytecode determinístico e executados pelo NI VM.
Ela NÃO embute Lua, NÃO usa Python, NÃO roda em camada de navegador e
NÃO tem JIT (nunca terá — ADR-049).

## Por que uma linguagem própria

- **Controle de semântica**: `repeat`/`repair`/`timeout` têm semântica
  FORMAL definida antes da implementação (docs/ni-script/04) — inclusive
  o orçamento de instruções que torna loop infinito impossível;
- **Sem dependência externa**: zero linhas de código de terceiros no
  caminho do gameplay — mesmo princípio do resto do motor;
- **Mobile-first**: VM pequena (uma TU), sem GC, sem threads, sem
  alocação por instrução no caminho quente;
- **Fronteiras limpas**: o núcleo da linguagem (`engine/niscript`)
  depende só de core/math/ecs(POD)/reflect; bindings de gameplay são
  registrados pelo CONSUMIDOR (editor), igual ao catálogo do
  SceneSerializer (ADR-043).

## Pipeline

```
.nis ─→ Lexer ─→ Parser ─→ Sema ─→ Compiler ─→ bytecode ─→ NI VM ─→ bindings
        tokens     AST      tipos     NiProgram   determinístico   C++
                  linha/col  símbolos  (imutável,              reflexão/
                             checagens compartilhado)           catálogo
```

Cada etapa é pura (sem I/O, sem relógio, sem estado global): mesma fonte
+ mesma tabela de nativos produzem o MESMO bytecode (testado).

## Um script de exemplo

```
add &BL

var velocidade: float = 2.0
var passos: int = 0

f empurra():
    var me = self()
    me.position.x = me.position.x + velocidade
stop

up update:
    repeat 3:
        empurra()
    stop
    passos = passos + 1
stop
```

O que este arquivo demonstra: import de módulo (`add &BL`), estado
global por instância (`var` no topo), função (`f … stop`), handler de
evento (`up update`), iteração controlada (`repeat`) e acesso a campos
da própria entidade via bindings (`me.position.x`).

## Módulos e eventos

- `add &MODULO` — importa módulo. Disponível em v1: **`&BL`**
  (biblioteca base — matemática pura e construtores, docs/07). Outros
  módulos são PLANEJADOS e NÃO implementados (`&UI`, `&NET`).
- Eventos: `up start` (1x ao entrar em play), `up update` (por tick),
  `up destroy` (best-effort no stop), `up <nome>` (custom — disparado
  por `emit` e propagado por `link to`, docs/02 §eventos).

## O que NÃO existe (v1 — decisões, não omissões)

| Recurso | Estado |
|---|---|
| Arrays/dicionários | não — v1 |
| `break`/`continue` | não — v1 |
| Corrotinas/`wait` | não — v1 |
| Timeout por tempo de jogo | não — v1 (orçamento de instruções, docs/04) |
| UI de script no editor | adiada — fonte editável via Inspector (docs/08) |
| Serialização de bytecode (cache) | não — recompilação por play |
| fault-info introspectável do script | não — `lastFault` é C++-only |
| JIT | nunca (ADR-049) |

## Mapa dos documentos

1. **01-overview** (este)
2. **02-syntax** — gramática, blocos `stop`, declarações, eventos
3. **03-types** — modelo de valores, inferência, zero-values
4. **04-control-flow** — semântica FORMAL de if/repeat/repair/timeout
5. **05-bytecode** — instruction set e codificação
6. **06-vm** — execução, orçamento, determinismo, faults
7. **07-bindings** — &BL, host, tabela de componentes, integração ECS
8. **08-tools** — diagnósticos, hooks de depuração, estado das ferramentas
