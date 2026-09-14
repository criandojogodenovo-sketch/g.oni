# ADR-021 — Estratégia de reflexão: registro explícito por macro

- **Estado:** aceito (FASE 2, missão §B.1)
- **Contexto:** o motor precisa de metadados de tipos para editor, serialização
  e scripting. A reflexão nativa de C++ (P2996) não existe em C++20 e RTTI é
  desativado no motor (ADR-005).

## Decisão

Reflexão por **registro explícito em tempo de inicialização estática**, via
macros `ENG_REFLECT*` usadas em escopo de namespace, fora da classe:

- `ENG_REFLECT(T)` — tipo opaco (nome/size/align).
- `ENG_REFLECT_BEGIN(T)` … `ENG_REFLECT_FIELD(m)` / `ENG_REFLECT_FIELD_AS(m, "tipo")` … `ENG_REFLECT_END()` — struct com propriedades (nome + offset + tipo).
- `ENG_REFLECT_ENUM_BEGIN(T)` … `ENG_REFLECT_ENUM_VALUE(E)` … `ENG_REFLECT_ENUM_END()` — enum com enumeradores.

`offsetof` exige layout padrão e campos públicos; o requisito é documentado
nos macros e violar é erro de compilação (comportamento indefinido de
`offsetof` em tipos não-standard-layout é UB detectável em revisão).

## Identidade de tipo

`TypeId = FNV-1a 64 do nome canônico` — determinístico entre execuções e
unidades de tradução, avaliável em `constexpr` (`typeIdOf`). Probabilidade de
colisão por par ≈ 2⁻⁶⁴; política de degradação: em colisão com nome distinto,
o segundo registro é descartado e o id existente devolvido (nunca sobrescreve,
 nunca aborta).

## Nomes canônicos

Primitivos usam nomes curtos (`i32`, `f32`, `u8`, `string`, …) mapeados pelo
trait `PrimitiveName<T>`, extensível por especialização em código de
integração (padrão usado por `eng::math` no futuro). Aliasing de plataforma
(`long` vs `long long`) colapsa em `i64`/`u64` — documentado, intencional:
nomes canônicos são do motor, não do compilador.

## Concorrência

- Escrita (`registerType`): lock exclusivo (`std::shared_mutex`).
- Leitura (`find`, `count`): lock compartilhado — **leituras concorrentes são
  seguras e testadas** (8 threads × 20k lookups).
- Registro esperado em startup (inicialização estática dos macros), leitura a
  qualquer momento. Registrar em runtime é suportado, mas ponteiros `TypeInfo*`
  são estáveis apenas até o próximo `clear()` (uso de `clear()` restrito a testes).

## Registro global (exceção justificada à regra de sem-singletons)

`TypeRegistry::global()` é um singleton (magic static). Justificativa: o
registro é declarativo e ocorre em inicialização estática espalhada por TUs —
não há ponto único de construção nem ordem determinística de chamada; o dono
do estado é o processo. Sem estado mutável global além deste (nenhuma variável
 global solta). A missão §B.0 exige ADR para singleton: este parágrafo é a
 dispensa registrada, válida enquanto não houver multi-registry comprovado.

## Idempotência

Macros em headers incluídos em múltiplas TUs geram re-registro: o primeiro
registro vence, metadados posteriores são descartados, o mesmo id é devolvido.
Re-registro com metadados diferentes não altera o original (testado).

## O que a reflexão NÃO faz (por decisão)

- **Membros privados** — `offsetof` exige layout público; a intenção é expor
  o contrato de dados, não violar encapsulamento.
- **Herança polimórfica automática** — sem RTTI (ADR-005); hierarquias serão
  representadas por propriedades explícitas quando a serialização exigir.
- **Invocação de métodos por metadado** — chamadas seguras virão na fase de
  scripting via ponteiros-de-função registrados explicitamente.
- **`std::vector<T>`** — §B.1 marca como opcional; adiado até o módulo de
  serialização definir o formato (evita metadados especulativos não testados
  end-to-end).

## Alternativas consideradas

- **RTTI/dynamic_cast** — excluído pelo ADR-005 e insuficiente (sem campos).
- **Reflexão externa (libclang/Clang AST)** — gera código automático, mas
  adiciona dependência de toolchain ao build e arquivos gerados a revisar;
  descartada para FASE 2.
- **Registrar via função manual sem macro** — a API pública
  `TypeRegistry::registerType` existe e é usada em testes; os macros são
  açúcar para o caso comum (offsets), não o único caminho.
