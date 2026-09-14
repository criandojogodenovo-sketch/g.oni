# eng::events — EventBus com Lifetime RAII (FASE 2)

> Barramento de eventos determinístico com cancelamento seguro durante o
> dispatch. Decisões completas: [ADR-022](../adr/ADR-022-events-lifetime.md).

## Posição no grafo

```
eng::core ──▶ eng::events
```

Sobre `eng::core` (missão §B.0); nenhum símbolo consumido nesta fase — o link
declara a camada.

## API essencial

```cpp
eng::events::EventBus bus;

// Inscreve (handler invocável como f(const E&)) — devolve RAII move-only:
auto sub = bus.subscribe<Explosion>([&](const Explosion& e) { /* ... */ });
// sub destruído/movido → inscrição cancelada; unsubscribe() idempotente.

bus.publish(Explosion{.radius = 3.5f});  // ordem de inscrição, sem alocação
std::size_t n = bus.subscriberCount<Explosion>();
```

## Semânticas garantidas (testadas)

- Ordem de execução = ordem de inscrição (determinística).
- Cancelar durante o dispatch: handler ainda não chamado nesta rodada → não
  roda; handler corrente → seguro (remoção física adiada ao fim da rodada).
- Inscrever durante o dispatch → não roda na rodada corrente.
- `publish` aninhado (mesmo/ outro tipo) suportado; rodada aninhada é uma
  rodada nova sobre as entradas comprometidas naquele momento.
- Handler: small-buffer 48 B + ponteiros de função (sem `std::function`);
  excedentes vão ao heap na inscrição; `publish` nunca aloca.

## Política de threads

**Não thread-safe** — operações em um mesmo bus devem vir da mesma thread ou
ser serializadas externamente (motivação e alternativa em ADR-022). Reentrância
na mesma thread é suportada e testada.

## Testes (15 casos)

Payload por const-ref; publish sem subscribers; ordem múltipla; tipos
independentes; handler com estado; lifetime destrutivo; unsubscribe
idempotente; movimento de Subscription; cancelamento de não-chamado durante
dispatch; auto-cancelamento; inscrição durante dispatch; reentrância mesma
thread (mesmo tipo); aninhamento cross-type; dispatch aninhado sem dangling;
handler heap; contagem de subscribers.
