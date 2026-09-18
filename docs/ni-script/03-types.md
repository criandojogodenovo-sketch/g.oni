# NI-Script — 03: Tipos e valores

> FASE 11 · regras canônicas: `phase11_audit/design.md §3`

## Os dez tipos

| Tipo | Valores | Zero-value (`var x: T`) |
|---|---|---|
| `int` | i64 com sinal | `0` |
| `float` | f64 IEEE-754 | `0.0` |
| `bool` | `true`/`false` | `false` |
| `string` | UTF-8 imutável | `""` |
| `vec2` | {x, y} f64 | `(0, 0)` |
| `vec3` | {x, y, z} f64 | `(0, 0, 0)` |
| `color` | {r, g, b, a} f64 (0..1 por convenção) | `(0, 0, 0, 1)` |
| `entity` | handle geracional (índice+geração) | entidade nula |
| `asset` | AssetId u64 (opaco) | `0` |
| `transform` | {position vec3, rotation vec3 GRAUS, scale vec3} | identidade |

Interno (não declarável): **CompView** — visão `entidade + prefixo de
caminho` acumulada por acessos `.` em entity; resolução acontece no uso
tipado (docs/07).

### Por que f64 no VM e f32 no motor

O VM calcula em f64 (determinismo entre plataformas — uma única
aritmética IEEE). As FRONTeiras de binding convertem para f32/i32 com
CHECAGEM de range/NaN/inf (Fault reparável — nunca silenciosamente
truncado). `float` do script ≠ `f32` do reflect.

## Inferência e compatibilidade

- `var x = 42` → `int`; `1.5` → `float`; `"a"` → `string`;
  `true` → `bool`;
- chamadas de FUNÇÃO retornam tipo DINÂMICO (retorno não declarado —
  checagem no uso); construtores de &BL são tipados
  (`vec2 → vec2`, …);
- `int ∘ int → int` (`+ - *`); qualquer lado `float → float`;
- `/`: int/int → int TRUNCADO para zero (`7/2 = 3`) — documentado;
  divisão por zero (int OU float) é **Fault**;
- `%`: só int (sinal do dividendo, como C++);
- `+` entre strings = concatenação; `string + int` NÃO compila
  (use `str(x)`);
- vetores: `v+w`, `v-w`, `v*escalar`, `escalar*v`, `-v`;
  `color`/`transform`/`entity`/`asset` NÃO têm aritmética;
- comparações `< <= > >=`: numéricos; `== !=`: TODOS os tipos
  (estrutural — vec compara componente a componente);
- `and or not`: só bool, com CURTO-CIRCUITO (RHS não avalia se o LHS
  decide);
- `var x: T = expr`: tipos estáticos devem CASAR exatamente
  (`var x: float = 1` é erro — escreva `1.0`);
- o tipo de `var` é FIXO na declaração; reatribuição incompatível
  conhecida estaticamente é erro de compilação.

## Dinâmico (o tipo `?`)

Leituras de campo de entidade (`e.position`) e resultados não tipáveis
são **dinâmicas**: propagam por cadeias de acesso (`.`) e são
checadas em RUNTIME nas operações tipadas (aritmética resolve e
verifica; atribuição a variável tipada emite `CHECK_TYPE`). Isso permite
acesso refletido a componentes sem o compilador conhecer o catálogo —
com o custo de falhas adiadas (repairáveis, docs/04).

## Campos por tipo estático

```
vec2.x .y            → float
vec3.x .y .z        → float
color.r .g .b .a    → float
transform.position .rotation .scale  → vec3 (rotation em GRAUS Euler XYZ
                                        — conversão na fronteira, mesma
                                        convenção do editor)
entity.<campo>      → dinâmico (resolvido pelos bindings — docs/07)
```

## entity e a segurança de geração

`entity` é um VALOR {índice, geração} — nunca um ponteiro. Um handle
obsoleto (entidade destruída, geração avançada — ADR-024) usado em
operação de leitura/escrita produz **EntityStale** (Fault reparável),
não UB. A entidade nula (`kNoEntityPacked`) é o zero-value;
`find(...)`/`spawn(...)` falham por Fault, não devolvendo nil —
nil NÃO EXISTE na linguagem (§nil, abaixo).

## nil — interno, nunca visível

Todo valor declarado tem zero-value ou inicializador. O estado `nil`
existe apenas INTERNAMENTE (retorno implícito de `f` sem `give`,
locais não inicializadas) e CONSUMÍ-Lo em operação tipada é Fault
`NilUse` — capturável por `repair` como qualquer outro. Isso elimina a
classe inteira de bugs null-propagation ao custo de exigir inicialização
explícita (design §3).
