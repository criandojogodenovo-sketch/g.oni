# NI-Script — 04: Semântica formal do fluxo de controle

> FASE 11 · REQUISITO da missão: "semântica definida formalmente ANTES da
> implementação". Fonte canônica: `phase11_audit/design.md §5`. A suite
> `niscript` testa CADA regra deste arquivo.

## 5.1 `if/else`

```
if C: B1 stop [ else: B2 stop ]
```
Avalia `C` (bool — checado estático quando conhecido, senão Fault
`Type`). `C` verdadeira executa `B1`; senão `B2` (se houver).
`var` em bloco é LOCAL ao bloco (escopo léxico aninhado; shadowing
permitido entre níveis, redeclaração no MESMO nível é erro).

## 5.2 `repeat` — iteração CONTROLADA

```
repeat N: B stop
```
1. `N` avalia UMA vez, antes da primeira iteração; deve ser `int`.
   Literal fora de `[0, 65536]` → erro de COMPILAÇÃO; valor dinâmico fora
   do range em runtime → Fault `RepeatLimit` (reparável).
2. `N <= 0`: o corpo não executa (válido).
3. Caso contrário o corpo executa EXATAMENTE N vezes.
4. **Loop infinito é impossível por construção**: cada instrução consome
   o orçamento global (docs/06 §orçamento) — esgotar = Fault `Timeout`.
5. `var` do corpo é REINICIALIZADA a cada iteração (escopo por
   iteração); variáveis externas mantêm valor entre iterações.

Não existe `break`/`continue` em v1 (futuro: `halt` — planejado, não
implementado).

## 5.3 `repair` — recuperação de falha

```
repair: G stop
```
`repair` é uma REGIÃO GUARDADA contra Faults de RUNTIME (nunca erros de
compilação):

1. Entrar na região salva: profundidade da pilha de valores, dos frames
   e o deadline de timeout vigente.
2. Se QUALQUER instrução de `G` gerar Fault — inclusive dentro de
   chamadas `f`, blocos aninhados e handlers `emit`-ados a partir de
   `G`:
   a. a execução de `G` abandona IMEDIATAMENTE;
   b. o VM restaura o estado salvo (frames internos descartados);
   c. a execução continua na PRIMEIRA instrução após o `stop` da região;
   d. o Fault fica registrado em `NiScriptState::lastFault`
      (consultável do C++ para diagnóstico — NÃO introspectável do
      script em v1).
3. **Atomicidade por instrução**: toda atribuição avalia o RHS COMPLETO
   antes da escrita e só há UM alvo por atribuição — a instrução que
   falha NÃO aplica escrita parcial. Efeitos de instruções ANTERIORES da
   região NÃO são desfeitos: **repair não é transação** (explícito).
4. Sem fault: a região é apenas um bloco (escopo próprio).
5. Fault fora de QUALQUER região: a execução do EVENTO corrente é
   abandonada, o Fault registrado e devolvido ao host; o script NÃO
   morre — o próximo evento roda normalmente (isolamento por evento).

## 5.4 `timeout` — orçamento de instruções (NÃO relógio)

```
timeout N: B stop
```
1. `N` avalia uma vez; int ≥ 1 (literal < 1 é erro de compilação;
   runtime < 1 → Fault `Timeout` imediato, reparável).
2. Define o ORÇAMENTO de `B`: `deadline = instruções_até_aqui + N`.
   Cada instrução dispatchada dentro de `B` (inclusive chamadas e
   emits a partir de `B`) incrementa o contador global; `contador >
   deadline` → Fault `Timeout`, reparável pela região `repair` mais
   interna que ENVOLVA o `timeout` (o próprio `timeout` NÃO captura).
3. **Por que instrução e não segundos**: timeout por tempo real exige
   thread/relógio/interrupção — quebra o determinismo e viola "sem
   threads escondidas, sem alocação por frame". Orçamento de instruções
   é determinístico, testável e não aloca. Timeout por TEMPO DE JOGO
   (relógio simulado em pontos de determinismo explícitos) é futuro
   planejado e NÃO implementado.
4. Aninhamento: o deadline mais APERTADO vige; sair do bloco restaura
   o anterior.

## Emissão (`emit`) — regras completas

1. O handler `up NOME` do script corrente executa SINCRONAMENTE
   (empilhado — a instrução `emit` só conclui após ele retornar).
2. Propagação BFS pelos links: handlers `up NOME` de scripts cuja
   entidade é alcançável a partir da entidade emissora, deduplicado por
   entidade visitada (ciclos seguros), ordem determinística (BFS pela
   ordem de inserção das arestas).
3. O emissor NÃO recebe o evento duas vezes (handler local + BFS são
   mutuamente exclusivos).
4. Reentrância permitida; profundidade de emissão ≤ 32 (Fault
   `EmitDepth` além — proteção de pilha).

## Links

- `link to E`: aresta dirigida da ENTIDADE DO SCRIPT para E na tabela de
  links da INSTÂNCIA; semântica de CONJUNTO (idempotente, sem reordenar);
  limite 64 (Fault `LinkLimit`); auto-link e entidade nula são Fault.
- Ordem de iteração: INSERÇÃO (vector — determinístico).
