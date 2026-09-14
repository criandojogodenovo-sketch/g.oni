# eng::jobs — JobSystem com Work Stealing (FASE 2)

> Pool de workers com deques próprios e roubo circular. Decisões completas:
> [ADR-023](../adr/ADR-023-jobs-architecture.md).

## Posição no grafo

```
eng::core ──▶ eng::jobs
```

## API essencial

```cpp
eng::jobs::JobSystem system;            // N = hardware_concurrency (mín. 1)
eng::jobs::JobSystem system(4);         // explícito (oversubscribe se quiser)

auto h = system.submit([]{ trabalho(); });  // → JobHandle (move-only RAII)
h.wait();                                   // bloqueia; repropaga exceção (debug)
system.waitAll();                           // filas + execução == 0
system.shutdown();                          // idempotente: drena, encerra, join
auto h2 = system.submit(f);                 // pós-shutdown: handle inválido,
h2.wait();                                  //   wait é no-op — sem crash
```

## Modelo de execução

- **Envio** round-robin; **execução local LIFO** (fim do deque);
  **roubo FIFO** (início do deque vizinho, varredura circular).
- Callable type-erased com small-buffer 48 B (sem `std::function`); maiores
  vão ao heap no envio; movimento sempre noexcept.
- Ciclo de vida: destrutor = `shutdown()` — rejeita envios, **drena as filas**
  (todo job pendente executa), join sem vazar threads. Workers encerram
  dinamicamente: jobs anexados por jobs em execução ainda são drenados.

## Exceções

`ENG_JOBS_CATCH_EXCEPTIONS` (ON em linux-debug, OFF em linux-release): ON —
exceção do job vira `exception_ptr` e repropaga em `wait()`, pool segue vivo;
OFF — exceção no worker encerra o processo (política de release). Testes de
exceção são condicionais ao flag.

## Limitações deliberadas (FASE 2)

- `wait`/`waitAll` de dentro de um job: não suportado (deadlock possível).
- JobGroup / dependências / futures / corrotinas: documentados em ADR-023
  como evolução — sem código stub.

## Verificações

17 casos Catch2 (ASan+UBSan+Werror): ciclo de vida, waits, stress 10k,
oversubscription, cascata job→job, shutdown com 100 pendências, submit
pós-shutdown, idempotência, handle pós-destruição, callable heap, exceções.
Adicionalmente: **TSan** dedicado (rajada + cascata + 2 waiters externos +
shutdown) — zero data races.
