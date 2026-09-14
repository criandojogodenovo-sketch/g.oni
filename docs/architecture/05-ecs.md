# eng::ecs — World com Sparse-Sets (FASE 2)

> Entidades com handles geracionais e componentes em sparse-sets. Decisões
> completas: [ADR-024](../adr/ADR-024-ecs-storage.md).

## Posição no grafo

```
eng::core ──▶ eng::ecs ◀── eng::reflect (aresta declarada, símbolos ainda não consumidos)
```

## API essencial

```cpp
eng::ecs::World world;

eng::ecs::Entity e = world.create();
world.emplace<Position>(e, 1.0f, 2.0f);   // cria/substitui; obsoleto → nullptr
Position* p = world.get<Position>(e);     // nullptr se ausente/obsoleto
bool has = world.has<Position>(e);
world.remove<Position>(e);                // false se ausente
world.destroy(e);                         // limpa TODOS os componentes; false se obsoleto

world.each<Position, Velocity>(           // entidades com AMBOS, ordem de inserção
    [](eng::ecs::Entity ent, Position& p, const Velocity& v) { /* ... */ });
constWorld.each<Position>(                // const World → const Position&
    [](eng::ecs::Entity, const Position&) {});
```

## Semânticas garantidas (testadas)

- Handles obsoletos: TODAS as operações são no-op seguro — sem crash, sem
  abort (missão §B.4).
- Reciclagem de índice com **geração incrementada** — handle antigo jamais
  vê a entidade nova do slot.
- Remoção por swap-and-pop com move-CONSTRUCT: componentes precisam ser
  move-constructible (noexcept preferido); move-only funciona.
- `each` por snapshot: mutação durante a iteração (destruir corrente/outra,
  remover componentes, criar novas) é segura; nenhuma entidade visitada duas
  vezes; novas não aparecem na rodada corrente.

## Complexidade

| Operação | Custo |
|---|---|
| create / destroy | O(1) + O(#pools com componente) |
| emplace/get/has/remove | O(1) amortizado |
| each<Ts...> | O(m) snapshot (m = menor pool) + O(m·#Ts) verificações |

## Testes (18 casos / 1602 asserções)

Ciclo de vida; reciclagem com geração; no-ops obsoletos; emplace/replace;
componentes move-only; each single/multi/const; ordem; mutação durante each
(4 cenários); estresse de 1000 entidades com reciclagem ampla.
