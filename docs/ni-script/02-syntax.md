# NI-Script — 02: Sintaxe

> FASE 11 · gramática canônica: `phase11_audit/design.md §2`

## Blocos terminam por `stop` — sem indentação, sem chaves

```
f nome(params):
    <comandos>
stop
```

`stop` fecha o bloco aberto mais recente (casamento ESTRUTURAL). Não
existe semântica de indentação e `{}` não existe. Indentação no fonte é
ESTILO recomendado (todos os exemplos usam 4 espaços), nunca sintaxe.

Comentários: `#` até o fim da linha (sem comentário de bloco em v1).

## Declarações de topo

```
add &BL                  # import de módulo (idempotente)
var velocidade: float    # global de instância (zero-value)
var passos = 0           # global com inferência
f soma(a: int, b: int): # função (params com tipo opcional)
    give a + b
stop
up update:               # handler de evento
    ...
stop
```

Regras:
- `var` no topo = estado GLOBAL da instância (persiste entre eventos,
  não é compartilhado entre entidades); `var` em bloco = local com
  escopo léxico aninhado;
- `var` exige **tipo OU inicializador** (erro de compilação sem nenhum);
- função/handler/global dividem o MESMO espaço de nomes — redeclaração
  é erro; LOCALS podem sombrear globais;
- nomes de TIPO (`int`, `vec3`, …) são CONTEXTUAIS: lexam como
  identificadores comuns e são reconhecidos apenas em posição de tipo —
  `vec3(1.0, 2.0, 3.0)` é o CONSTRUTOR de &BL e `var v: vec3` é a
  anotação (decisão v1; consequência: variáveis PODEM se chamar `vec3`,
  o que é aceito e documentado);
- `give` só é permitido dentro de `f` (erro em `up`/topo).

## Comandos

| Forma | Semântica (detalhe em docs/04) |
|---|---|
| `if C:` B `stop` [`else:` B `stop`] | condicional — C deve ser bool |
| `repeat N:` B `stop` | iteração CONTROLADA — N ∈ [0, 65536] |
| `repair:` B `stop` | região guardada contra Faults |
| `timeout N:` B `stop` | orçamento de N instruções para B |
| `link to E` | aresta dirigida para a entidade E (set, ≤64) |
| `emit evento` | dispara handler local + propagação BFS |
| `give [expr]` | retorno de função |
| `alvo = expr` | atribuição (RHS completo antes da escrita) |
| `expr` | expressão-sentença |

**Atribuição**: o alvo é `IDENT` ou cadeia de campos a partir de IDENT
(`me.position.x`). Atribuir em cadeia com raiz DINÂMICA de 2+ campos é
erro de compilação — anote a variável (`var p: vec3 = e.position`).

## Expressões

Precedência (menor → maior):

```
or
and
not                 (prefixo)
==  !=  <  <=  >  >=  (não associativo — um por expressão)
+  -                (e concatenação "a" + "b")
*  /  %
-  not              (unário)
chamada  campo      (postfix: nome(args), .campo)
literais  ( )  nome
```

Literais: `42` (int), `1.5` (float), `"texto"` (string, escapes
`\n \t \" \\`), `true`/`false`. **Não existe literal `nil`**.

Nomes de CAMPO de componentes seguem a mesma lexagem: campos cujo nome
colida com uma keyword (`f`, `stop`, `up`, `if`, `add`, …) não são
acessíveis pela sintaxe de ponto — renomeie o campo do componente
(documentado em docs/07 §limitações).

## Eventos e links

- `up NOME:` declara o handler; evento duplicado no mesmo script é erro
  de compilação;
- `emit NOME`: executa o handler `up NOME` **do script corrente
  (sincronamente)** e propaga por BFS aos scripts alcançáveis pelos
  links (deduplicado por entidade visitada — ciclos são seguros);
  profundidade de emissão limitada a 32 (Fault além);
- `link to E`: idempotente (duplicata não reordena), auto-link é Fault;
  ordem de iteração é a ordem de INSERÇÃO (determinístico).

## Exemplo completo de eventos

```
add &BL

var hitCount: int = 0

up ping:
    hitCount = hitCount + 1
stop

up update:
    if action_pressed("jump"):
        emit ping
    stop
stop
```
