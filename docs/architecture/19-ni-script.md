# 19 — NI-Script (FASE 11)

> Linguagem de script própria do G.oni. ADR-049. Documentação completa:
> `docs/ni-script/` (8 arquivos). Design formal pré-implementação:
> `phase11_audit/design.md`.

## Papel no motor

Camada de GAMEPLAY sobre ECS/reflect — o quebra-cabeça que faltava entre
"os dados da cena" e "o comportamento por entidade". Sem Lua, sem
Python, sem navegador, sem JIT: `.nis` → bytecode próprio → NI VM.

```
engine/niscript (nova)
   ├─ dependências: core, math, ecs (Entity POD), reflect
   ├─ NÃO conhece: rhi, android, editor, physics, input, audio
   └─ consumers: editor (NiRuntime no play), tests
```

## Módulo `engine/niscript`

| Peça | Arquivos | Responsabilidade |
|---|---|---|
| Valores/faults | `NiValue.hpp/.cpp` | modelo de valor, zero-values, limites |
| Pipeline | `NiScript.hpp` + `Lexer/Parser/Sema/Compiler.cpp` | `.nis` → `NiProgram` puro com diagnósticos linha/col |
| Bytecode | `NiProgram.hpp` | consts/funcs/handlers/globals + sourceMap |
| VM | `NiVm.hpp` + `Vm.cpp` | execução determinística com orçamento, repair/timeout, emit BFS |
| Bindings | `NiBindings.hpp/.cpp` | &BL, host natives, tabela refletida por offset |

Arestas do grafo (atualização de 00-overview): `niscript → core, math,
ecs, reflect` — nenhum módulo engine depende de niscript; o EDITOR é
quem liga (igual physics/animation/particles).

## Decisões estruturais

1. **Sema antes do código**: a semântica de `repeat/repair/timeout` foi
   escrita e congelada em `phase11_audit/design.md §5` ANTES da
   implementação (requisito explícito da missão); a suite testa cada
   regra individualmente.
2. **Orçamento de instruções, não relógio**: todo evento roda sob
   orçamento (default 1M) — loop infinito é IMPOSSÍVEL de forma
   determinística, sem threads nem alocação por frame (docs/ni-script/04).
3. **Fault ≠ exceção**: falha de runtime desmonta até o `repair` mais
   interno ou aborta SÓ o evento corrente; o script continua vivo
   (isolamento por evento — ADR-004 sem exceções).
4. **Bindings no consumidor**: `NiBindingTable` é registrada por quem
   executa (ADR-043 pattern); o adaptador refletido reusa
   offsets+typeName do TypeRegistry — o MESMO mecanismo do Inspector
   (auditoria D2, zero hard-code no engine de script).
5. **Geração respeitada**: `entity` é handle geracional VALUE; leitura
   em handle obsoleto = `EntityStale` Fault (ADR-024 propagado).
6. **Sem nil na linguagem**: zero-values + Fault NilUse no consumo
   interno — elimina a classe null-propagation inteira.

## Integração com o editor (FASE 8/ADR-044 estendidos)

- `NiScriptComponent {source}` no catálogo ÚNICO do SceneSerializer
  (registrado em `editor/src/ComponentRegistration.cpp`) — persiste em
  cena, editável via Inspector (string) e JNI;
- `NiRuntime` (editor) compila os scripts do CLONE no `play()`, roda
  `@init` → `up start`, `up update` por `tick()`, `up destroy` no
  `stop()`; bindings apontam para o world do clone e MORREM com ele;
- erro de compilação em play: script desabilitado + log (cena segue);
- faults de runtime: registrados em `lastFault` (C++-only em v1).

## Testes (resumo — detalhe na suite)

- 58 casos / 516+ asserções: lexer/parser/sema, VM, semântica §5
  (CADA regra), bindings/ECS (geração, ranges, ausências), **E2E**
  `.nis→compile→bytecode→VM→binding→mudança no ECS`, determinismo
  byte-a-byte, segurança (nativos fechados, orçamento, sem nil),
  ferramentas (linha/coluna, trace);
- editor: play com scripts (start/update/destroy, edição intacta),
  script quebrado não derruba o play.

## Estado (honesto)

IMPLEMENTADO + UNIT/INTEGRATION TESTED (Linux debug com
ASan/UBSan/LSan, release LTO) + CI Linux/Android + APK empacotado.
UI de script no editor: ADIADA e declarada (docs/ni-script/08).
DEVICE TESTED (Realme C33): NÃO — sem dispositivo na execução desta fase.
