# NI-Script — 05: Bytecode

> FASE 11 · ver `engine/niscript/include/eng/niscript/NiProgram.hpp`

## Estrutura do programa compilado

```cpp
struct NiProgram {
    std::vector<NiConst>   consts;    // pool: Int/Float/Bool/String/FieldChain
    std::vector<NiFunc>   funcs;     // funções E corpos de handlers
    std::vector<NiHandler> handlers;  // evento → funcIndex
    std::vector<NiGlobal>  globals;   // nome+tipo+zero-value
    std::vector<std::string> modules; // ["BL"]
    std::size_t nativeCount;          // SANITY vs tabela do runtime
};
```

- Handlers são FUNÇÕES de aridade 0 nomeadas `@evento` (o `@` não é
  lexável — sem colisão com nomes do usuário);
- `@init` é um handler IMPLÍCITO com os inicializadores globais, na
  ordem de declaração; roda 1x ao instanciar (docs/07 §ciclo);
- cada `NiFunc` carrega `nparams`, `nlocals`, `paramTypes` e o
  **sourceMap** (`lines`/`cols` paralelos ao código) — todo Fault de
  runtime sai com linha/coluna.

## Codificação

Instruções: `struct NiInstr { OpCode op; uint32 a, b; }` (12 bytes +
padding). Desvios são RELATIVOS: `pc += int32(a)` com pc já avançado
(`a = alvo − (posição+1)`). `ENTER_REPAIR.a` é o pc ABSOLUTO da
continuação. `CALL_N.b` carrega o argc REAL (nativos variádicos).

## Instruction set

| Opcode | Operandos | Pilha | Semântica |
|---|---|---|---|
| `CONST` | a=idx | +1 | push consts[a] |
| `ZERO` | a=NiType | +1 | push zero-value do tipo (entity nula, transform identidade, nil interno…) |
| `LOAD_L`/`STORE_L` | a=slot | ∓/+1 | local do frame (base+slot) |
| `LOAD_G`/`STORE_G` | a=slot | ∓/+1 | global da INSTÂNCIA dona do frame |
| `CHECK_TYPE` | a=NiType | 0 | pop; resolve CompView; tipo≠T → Fault; push |
| `ADD SUB MUL` | — | −1 | numérico/string/vec (docs/03) |
| `DIV MOD` | — | −1 | numérico; /0 e %0 → Fault |
| `NEG` | — | 0 | −numérico/−vetor |
| `NOT` | — | 0 | bool |
| `EQ NE` | — | −1 | igualdade estrutural (SEM resolve) |
| `LT LE GT GE` | — | −1 | numéricos |
| `JMP/JMPF` | a=rel | JMPF −1 | salto relativo; JMPF checa bool |
| `CALL_F` | a=func | 0 | chamada (args já na pilha) |
| `CALL_N` | a=idx, b=argc | −argc+1 | nativo (índice VALIDADO em compile) |
| `GIVE` | — | 0 | retorno (trunca ao base do frame) |
| `POP` | — | −1 | descarta |
| `VEC_GET` | a=NiField | 0 | componente estático de vec/color/transform |
| `NEST_SET` | a=chain | −1 | escrita em cadeia de campos de VALOR (cópia modificada) |
| `DYN_GET` | a=str | 0 | entity→CompView; CompView acumula; valor→componente |
| `DYN_SET` | a=str | 0 | CompView/Entity→binding write-through (push base); valor→cópia modificada (push) |
| `TO_ENTITY` | — | 0 | Entity→passa; CompView→extrai entity (normaliza slot raiz) |
| `SELF` | — | +1 | handle da entidade dona da instância |
| `LINK_TO` | — | −1 | link (set, ≤64, sem auto-link) |
| `EMIT` | a=str | 0 | handler local + BFS (docs/04) |
| `ENTER_REPAIR`/`EXIT_REPAIR` | a=cont | — | região guardada |
| `ENTER_TIMEOUT`/`EXIT_TIMEOUT` | — | −1/+0 | orçamento (deadline apertado vige) |
| `REPEAT_INIT` | a=rel | −1/+1 | valida [0,65536]; 0 → pula o loop; senão contador na pilha |
| `REPEAT_STEP` | a=rel | 0 | contador−1; >0 → volta ao corpo; 0 → pop |

## Regras de pilha

- frames: `base = topo − nparams`; locais em `[base, base+nlocals)`;
  temporárias acima; corpo de `repeat` mantém o CONTADOR no topo (corpo
  é balanceado — invariantes do compilador);
- `GIVE`/retorno implícito trunca a pilha ao base e desfaz as REGIÕES
  do frame (repair/timeout nunca vazam de um frame);
- limite: 65.536 slots de valor, 256 frames (Fault `Stack`).

## Invariantes de segurança do bytecode

1. Índices (const/func/native/slot/global) só existem se VALIDADOS pelo
   compiler — o VM nunca re-valida existência, só limites internos;
2. `nativeCount` é conferido contra a tabela do runtime em CADA execução
   (divergência = erro de configuração do HOST, nunca índice trocado);
3. O bytecode só é produzido pelo compiler (não há loader externo em
   v1 — serialização de bytecode é futuro planejado).
