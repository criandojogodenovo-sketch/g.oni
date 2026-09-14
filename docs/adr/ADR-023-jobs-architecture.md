# ADR-023 — Jobs: work stealing com deques por worker e ciclo de vida definitivo

- **Estado:** aceito (FASE 2, missão §B.3)
- **Contexto:** a engine precisa paralelizar trabalho por frames e cargas de
  assets (fases futuras) com um pool próprio, sem depender de bibliotecas
  externas, com shutdown correto e propagação de erros.

## Decisão

### Topologia: work stealing REAL

- N workers (default `hardware_concurrency()`, mínimo 1), **um deque por
  worker** protegido por mutex (`std::deque<Task>`).
- Envio externo: round-robin atômico entre deques.
- Execução local: **LIFO** (pop do fim — localidade de cache, subárvores de
  tarefas first).
- Roubo: varredura circular a partir do vizinho, **FIFO** (início do deque da
  vítima — tarefas mais antigas first, reduz competição com o dono).
- Locks por deque (não Chase-Lev lock-free): contenção só quando a vítima e o
  ladrão disputam o MESMO deque; caminho comum (pop do próprio deque) não
  disputa com outros workers. Evolução documentada: Chase-Lev quando perf
  exigir — a interface (submit/handle/wait) não muda.

### Estado de conclusão e handles

- `JobState` (mutex + condition_variable + done + exception_ptr) é
  propriedade **genuinamente compartilhada** (fila/worker ↔ handle do
  usuário): `shared_ptr` explícito, justificado — o estado precisa sobreviver
  ao JobSystem para que `wait()` pós-destruição funcione.
- `JobHandle` move-only; destruição NÃO espera (o job continua — o estado é
  compartilhado). Pontos de sincronização: `wait()`, `waitAll()`, destrutor.
- `submit` pós-shutdown devolve handle **inválido**; `wait()` nele é no-op.
  Sem crash — comportamento definido e testado.

### Ciclo de vida, ordas e contabilidade linearizável

- Contadores atômicos: `queued_` (contados — ainda não publicados + nas
  filas) e `inFlight_` (executando). **Protocolo anti-janela-zero**:
  (a) o submit incrementa `queued_` ANTES de publicar o job na fila;
  (b) a transferência fila→execução incrementa `inFlight_` ANTES de
  decrementar `queued_`. Consequência: a soma `queued_ + inFlight_` só
  DECRESCENTA na conclusão de um job — nunca toca zero durante uma
  transferência — logo `waitAll` é linearizável em relação a submits e
  execuções concorrentes e jamais retorna cedo.
- Workers **estacionam em `std::counting_semaphore`** (contagem persistente):
  `release()` antes de `acquire()` não se perde — a categoria lost-wakeup de
  condition-variables não existe por construção; não há spin de predicado;
  e o envio NÃO disputa o mutex de wake (só o lock do deque alvo).
  Tokens residuais (worker consumiu token de job furtado por outro) são
  inócuos: o semáforo é mecanismo de despertar, não de contabilidade.
- `waitAll` espera `queued_ + inFlight_ == 0` em condition variable própria;
  toda transição da soma a zero é uma conclusão, que notifica sob
  `wakeMutex_` — sem lacuna de notificação.
- `shutdown()`: rejeita novos envios, notifica esperas, `release(N)` do
  semáforo para acordar todos os estacionados, espera a drenagem e faz
  **join** de todos os threads. Idempotente (flag atômica). O destrutor
  chama shutdown — nenhum thread vaza. A saída do worker é DINÂMICA:
  jobs anexados tardiamente (por jobs em execução) ainda são drenados.
- `submit()` concorrente com `shutdown()` a partir de threads EXTERNAS deve
  ser serializado pelo chamador (jobs anexados após a última verificação de
  drenagem de todos os workers não são executados). Uso interno (jobs
  submetendo jobs) é seguro: o worker que submete ainda está vivo e re-avalia
  a drenagem.
- `wait()`/`waitAll()` de DENTRO de um job: não suportado nesta fase (pode
  deadlockar o pool — especialmente com 1 worker). JobGroup/dependências é a
  evolução prevista.

> **Correção registrada (bug real, capturado pelo teste de stress):** a
> primeira implementação incrementava `queued_` APÓS o push e
> decrementava `queued_` no pop ANTES de incrementar `inFlight_` — duas
> janelas em que `queued_ + inFlight_ == 0` com job em trânsito. Sintoma:
> `waitAll` retornava cedo (~1 falha em 10 execuções do stress de 10k sob
> ctest paralelo; `counter == 9999`). Diagnóstico por repetição com captura
> do caso falho; correção pelo protocolo (a)/(b) acima + semáforo.
> Verificação: 45 execuções consecutivas do suite completas + TSan limpo.

### Exceções (isenção registrada ao ADR-004)

`ENG_JOBS_CATCH_EXCEPTIONS` (CMake, default ON; linux-debug ON, linux-release
OFF): quando ON, `Jobs.cpp` compila com `-fexceptions` (aplicado APÓS os
flags do módulo — última flag prevalece no GCC) — try/catch converte a
exceção do job em `exception_ptr`, armazenada no JobState e repropagada em
`JobHandle::wait()`. Quando OFF (release), exceção em job propaga no worker e
encerra o processo (`std::terminate`) — política de release. Testes de
exceção são compilados condicionalmente ao mesmo flag.

### Callable sem std::function

Type-erasure com small-buffer de 48 bytes + 3 ponteiros de função — mesma
família de solução do `eng::events` (ADR-022), mas MOVÍVEL (tarefas migram
entre filas): inline exige nothrow-move; heap move = transferência de
ponteiro. Movimento sempre noexcept.

### Interface de evolução — SEM stubs

JobGroup (agregação com espera coletiva), dependências entre jobs
(DAG/toposort), futures promessados e corrotinas são **documentados aqui
apenas**. Nenhum código stub é mantido: quando implementados, ganham testes
próprios e ADR próprio.

## Verificações

- 17 casos / ~60 asserções Catch2 (debug, ASan+UBSan+Werror), incluindo:
  stress 10k jobs, cascata job→job, shutdown com 100 pendências drenadas,
  submit pós-shutdown, handle pós-destruição, exceções.
- **45 execuções consecutivas do suite completo** (ctest paralelo, 2 núcleos)
  sem falhas após a correção do protocolo de contabilidade (o flake
  intermitente do stress desapareceu).
- **TSan** (GCC `-fsanitize=thread`, harness dedicado fora do preset):
  rajada 10k + cascata + 400 jobs com 2 waiters externos + shutdown —
  **zero data races** (re-executado após a mudança para o semáforo).

## Alternativas consideradas

- **Fila global única (MPMC)** — rejeitada como solução final: contenção
  centralizada; o roubo por deque distribui a contenção. (Mencionada na
  missão como fallback aceitável — o work stealing foi escolhido por ser a
  solução-alvo e igualmente verificável.)
- **Chase-Lev lock-free deques** — adiada: correção de memória lock-free em
  ADR separado quando o perfil exigir; mutex-por-deque é mensurável e correto.
- **std::async/std::thread por job** — rejeitado: custo de criação por job
  e sem shutdown drenável.
