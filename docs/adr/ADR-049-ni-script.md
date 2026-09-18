# ADR-049 — NI-Script: linguagem própria compilada para bytecode

- **Status**: ACEITO (FASE 11)
- **Data**: 2026-09-18
- **Contexto**: FASE 11 do roadmap exige uma camada de scripting de
  gameplay integrada ao ECS/ reflexão, segura e determinística, para o
  editor nativo e o runtime Android.

## Decisão

Implementar **NI-Script**: linguagem de script própria (extensão
`.nis`), compilada UMA vez para bytecode executado por uma VM interna.

1. **Sem engines de script de terceiros.** Nada de Lua/Python/JS:
   - o controle total da semântica é requisito (`repeat` controlado,
     `repair` com atomicidade por instrução, `timeout` por orçamento de
     instruções — formalizados ANTES em `phase11_audit/design.md §5`);
   - zero dependência externa no caminho crítico do gameplay (mesmo
     princípio do motor inteiro);
   - superfície de segurança auditável em ~2k linhas em vez de milhares
     de linhas de bindings sobre um runtime alheio.
2. **Sem JIT — nunca.** Compilação AOT dentro do processo (por play),
   bytecode de pilha interpretado. JIT quebraria o orçamento
   determinístico e a auditabilidade; o gargalo do gameplay mobile não
   é o interpreter de script.
3. **Orçamento de instruções como guardião global.** Toda execução de
   evento tem budget (1M default); loop infinito é impossível POR
   CONSTRUÇÃO, sem threads/relógios (ADR de determinismo).
4. **Camadas**: núcleo da linguagem (`engine/niscript`) sobre
   core/math/ecs/reflect; bindings de COMPONENTES registrados pelo
   CONSUMIDOR (ADR-043 pattern) reusando o TypeRegistry por offset
   (mesmo mecanismo do Inspector — reuso, zero duplicação).
5. **Sintaxe própria `f…stop`/`up`/`link to`/`add &M`**: reconhecível
   para o usuário-alvo (criador mobile), tokens curtos para digitação
   em tela pequena; semântica de indentação NUNCA (casamento por `stop`
   estrutural); tipos CONTEXTUAIS (nomes de tipo lexam como
   identificadores — permite `vec3(...)` construtor e `var v: vec3`).
6. **Sem nil na linguagem**: zero-values + Fault `NilUse` interno.
   Elimina a classe de bugs null-propagation ao custo de inicialização
   explícita — troca consciente.
7. **VM single-thread** por instância de host (a do editor roda na
   thread do frame). Paralelismo de scripts via jobs é FUTURO não
   planejado nesta fase (ver Alternativas).

## Alternativas consideradas

- **Lua/sol2**: madura, mas semantics escape (metatables/coercions),
   GC pauses em gameplay, +dependência; o requisito de semântica
   formal de repair/timeout não encaixava.
- **Python (pybind/wasm)**: runtime pesado para arm64 mobile, GIL,
   ABI frágil com -fno-rtti do motor.
- **WASM como camada de script**: bootstrap grande; ferramentas
   mobile-first deficientes; debugging de bytecode alheio difícil.
- **Interpreter de árvore (sem bytecode)**: diagnostics de runtime sem
   sourceMap, custo por nó na execução e difícil de orçamentar por
   instrução (medição menos previsível que contagem de opcodes).
- **Registro VM em vez de pilha**: mais rápida, porém significativamente
   mais complexa de validar; a VM de pilha atende o throughput alvo de
   scripts de gameplay.

## Consequências

- (+) Semântica 100% sob controle do projeto, testada regra a regra
  (58 casos/516+ asserções incl. E2E e determinismo byte-a-byte);
- (+) Segurança por construção: nativos fechados em compile-time,
  handles geracionais, orçamento obrigatório, sem I/O no VM;
- (+) Integração natural com o catálogo/reflexão existentes (D2);
- (−) Custo de manutenção de UMA linguagem (lexer→vm) — mitigado pelo
  escopo deliberadamente pequeno (10 tipos, 4 construtos de fluxo);
- (−) Ecossistema/ferramentas têm de ser construídos internamente
  (docs/ni-script/08 declara honestamente o que NÃO existe);
- (−) Recompilação por play (sem cache de bytecode) — barata no
  tamanho de script visado; cache é futuro planejado.

## Referências

- Design formal: `phase11_audit/design.md` (§5 = semântica do fluxo)
- Especificação da linguagem: `docs/ni-script/02…07`
- Integração editor/runtime: `docs/architecture/19-ni-script.md`
- Bugs reais achados pelos testes durante o desenvolvimento:
  ordem de avaliação de argumentos no lexer (keyword+move), posse do
  AST (arena), profundidade de emit por frame, registro de fault
  capturado, normalização de slot raiz (TO_ENTITY) — todos corrigidos
  e cobertos por testes de regressão na suite.
