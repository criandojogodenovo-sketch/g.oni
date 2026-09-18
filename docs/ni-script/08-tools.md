# NI-Script — 08: Diagnósticos e ferramentas

> FASE 11 · estado HONESTO das ferramentas (missão: "o que existe,
> o que é futuro — sem inflar")

## Diagnósticos de compilação (ENTREGUE)

Toda etapa do pipeline devolve `NiDiag {linha, coluna, mensagem}` —
coletados em vetor pelo `ni::compile(..., &diags)`:

- **Lexer**: string não terminada (posição do abre-aspas), escape
  desconhecido, caractere inesperado, literal fora de range;
- **Parser**: `stop` sem bloco aberto, `else` sem `if`, `:`/`(`/`)`
  esperados — posição do token ofensor;
- **Sema**: tipo incompatível (com os DOIS tipos na mensagem), nome
  desconhecido, redeclaração, aridade, `give` fora de `f`, `link to`
  não-entity, `add &Módulo` desconhecido, repeat/timeout literal fora
  de range, campo estático inválido — SEMPRE linha/coluna.

Colunas contam BYTES UTF-8 (não pontos de código) — limitação
documentada; segmentação unicode é futura.

## Faults de runtime (ENTREGUE)

`NiFault {kind, message, line, col}` — a linha/coluna vem do sourceMap
da função EM EXECUÇÃO (inclusive dentro de chamadas/emissões).
`NiScriptState::lastFault()` expõe o ÚLTIMO fault (capturado ou
abortante) para o host — o editor loga; scripts NÃO leem faults em v1.

## Hook de trace (ENTREGUE — primitiva)

```cpp
vm.setTraceHook([](const NiExecContext::TraceInfo& i) {
    // pc, linha, coluna, opcode, stackDepth, frameDepth
}, /*stride=*/100);
```
Callback por instrução (visão const). `stride=0` desliga. É a base
para tracers/profilers — NENHUMA ferramenta construída sobre ele foi
entregue em v1 (abaixo).

## O que NÃO existe ainda (futuro declarado, sem data)

| Ferramenta | Estado |
|---|---|
| UI de edição de script no editor Android | NÃO entregue — a fonte `.nis` é editável pelo campo `source` do componente (Inspector/string, funciona hoje via JNI); editor com realce/numeração é FUTURO |
| Breakpoints | NÃO entregue — o hook de trace permite um tracer de linha, mas breakpoint com pausa não existe |
| Inspetor de variáveis do VM | NÃO entregue — os globais são legíveis do C++ (`state.global(nome)`) mas não há UI |
| Profiler de scripts | NÃO entregue |
| REPL / CLI `ni` | NÃO entregue |
| Source maps para bytecode serializado | NÃO aplicável — bytecode não é serializado em v1 |
| Pretty-printer de AST/bytecode | parcial: `scripts/nidbg.cpp` (repo, ferramenta de DEV interna do motor) imprime o bytecode — não é produto |

## Como depurar HOJE (receita prática)

1. Erro de compilação: pegue o vetor `diags` (o editor loga linha:col +
   mensagem);
2. Fault de runtime: `NiScriptState::lastFault()` (kind + linha/col);
3. Execução: ligue o trace hook com stride 1 num harness de teste e
   siga pc/linha/opcode;
4. Estado: leia globais por `state.global("nome")` entre ticks.

## Convenções de mensagem

- Mensagens em PORTUGUÊS (consistente com o resto do motor);
- valores dinâmicos são citados: `nome desconhecido: nada`,
  `binding desconhecido: 'velocty'`;
- faults carregam linha/coluna APENAS do código do script (nunca do
  C++ do host).
