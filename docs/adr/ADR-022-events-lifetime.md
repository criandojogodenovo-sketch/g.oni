# ADR-022 — Events: lifetime por Subscription RAII e política de rodadas

- **Estado:** aceito (FASE 2, missão §B.2)
- **Contexto:** sistemas de gameplay (ecs/scene) precisam reagir a eventos
  (criação/destruição de entidades, mudanças de hierarquia) com lifetime
  controlado e dispatch determinístico. `std::function` no hot path é
  proibido pela missão (alocação/vtable indireta por inscrição).

## Decisão

### Type-erasure sem std::function

Handler = classe com **small-buffer de 48 bytes** + 3 ponteiros de função
(invoke/destroy + flag heap). Handler que cabe no buffer e no alinhamento é
construído IN-PLACE no buffer; maior/excedente vai ao heap na inscrição
(`::operator new`). `publish` **nunca aloca**: chamada direta via ponteiro de
função. Cópia e movimento deletados — o Handler vive em nós de `std::list`
(endereços estáveis) e nunca migra.

### Rodadas de dispatch (publish)

- Cada slot mantém `std::list<Entry>` — **ordem de inscrição = ordem de
  execução** (determinístico, testado).
- Uma rodada percorre da primeira entrada até a **última entrada comprometida
  no início da rodada** (inclusive). Inscrições anexadas durante a rodada
  esperam a próxima — "inscrito durante o dispatch não roda nesta rodada".
- Reentrância: `publish` aninhado (mesmo tipo ou outro) é suportado. Uma
  rodada aninhada é uma rodada nova sobre as entradas comprometidas NAQUELE
  momento — entradas inscritas durante a rodada externa, porém antes da
  aninhada, EXECUTAM na aninhada (estão comprometidas quando ela começa).
  Recursão infinita é erro do usuário (sem limite interno).

### Cancelamento e lifetime

- `Subscription` é **move-only RAII**: destruição/movida-de/`unsubscribe()`
  cancelam; cancelamento idempotente (após cancelar, o Subscription esquece a
  entrada — `entry_ = nullptr`).
- Cancelamento durante dispatch ativo: entrada vira **tombstone** (`dead`);
  a remoção física ocorre quando a profundidade do slot volta a zero (varredura
  no destrutor do guarda de profundidade — também em unwinding por exceção em
  builds de teste). Invariante central: `dead ⇒ nenhuma Subscription aponta a
  entrada` — portanto a varredura nunca libera memória em uso.
- Handler que cancela a SI PRÓPRIO: seguro — o objeto Handler permanece vivo
  até o fim da rodada (remoção adiada), a invocação termina normalmente.
- **Pré-condição:** Subscription NÃO pode sobreviver ao EventBus que a criou
  (o slot é destruído com o bus). Proprietário documentado, sem custo de
  `shared_ptr`.

### Chave por tipo sem RTTI

`eventKey<E>()` = endereço de membro estático `constexpr` inline por
instanciação de template — único e estável por tipo em todo o processo, sem
`typeid` (ADR-005). Slots por tipo em `unordered_map<const void*, unique_ptr<SlotBase>>`.

### Política de threads

EventBus **NÃO é thread-safe**: todas as operações em um mesmo bus
(subscribe/publish/cancelamento) devem ocorrer na mesma thread ou ser
serializadas externamente. Motivo: publish executa handlers arbitrários —
segurar locks internos durante handlers causaria deadlock reentrante; copiar
handlers para snapshot viola move-only e o custo do hot path. A ponte
inter-threads será papel de `eng::jobs` (fila de tarefas), não do bus.
Reentrância NA MESMA thread é totalmente suportada (é o caso testado).

### Complexidade

| Operação | Custo |
|---|---|
| subscribe | O(1) alocado (nó de lista + possível heap do handler) |
| publish | O(1) lookup + O(inscrições vivas) chamadas — sem alocação |
| unsubscribe fora de dispatch | O(n) busca linear no slot (operação fria) |
| unsubscribe durante dispatch | O(1) tombstone |

### O que deliberadamente NÃO há

- Entrega assíncrona/entre threads (futuro: integração com `eng::jobs`).
- Prioridades/filtros de handler (ordem pura de inscrição).
- Eventos mutáveis (handlers recebem `const E&` — sem pipeline de mutação).

## Alternativas consideradas

- **`std::function` por inscrição** — rejeitado: alocação potencial por
  inscrição, indireção dupla, sem controle de layout (missão §B.2 proíbe).
- **Snapshot por cópia a cada publish** — rejeitado: O(n) alocação no hot path
  e viola handlers move-only.
- **Vector de inscrições com índices + geração** — rejeitado: realocação do
  vector durante dispatch exige indireção extra por entrada; `std::list` dá
  estabilidade de endereço por construção.
