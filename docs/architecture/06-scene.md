# eng::scene — Hierarquia de Nós sobre o ECS (FASE 2)

> Floresta de nós com pai/filhos e transforms compostos. Decisões completas:
> [ADR-025](../adr/ADR-025-scene-hierarchy.md).

## Posição no grafo

```
eng::core ──▶ eng::scene ◀── eng::ecs ◀── eng::reflect (aresta declarada)
                   eng::math ──▶ eng::scene
```

## API essencial

```cpp
eng::scene::Scene scene;

auto pai  = scene.createNode();          // raiz, transform identidade
auto filha = scene.createNode();
scene.attach(filha, pai);                // false: inválido/self/ciclo
scene.detach(filha);                     // vira raiz

scene.localTransform(filha)->position = {1.0f, 0.0f, 0.0f};

eng::math::Mat4 m = scene.computeWorldMatrix(filha);   // sobe os pais, O(prof.)
scene.updateWorldTransforms();                         // em lote, iterativo
const eng::math::Mat4* cached = scene.worldMatrix(filha); // último update

scene.eachChild(pai, [](eng::ecs::Entity child) {});   // ordem de anexação
scene.destroyNode(pai);                  // cascata: subárvore inteira
```

## Semânticas garantidas (testadas)

- `attach` move o filho e rejeita ciclos (árvore intacta); idempotente.
- `destroyNode` derruba descendentes (folhas primeiro) e preserva pai/irmãos.
- Bypass (`world().destroy` direto) é tolerado: referências obsoletas são
  puladas e limpas oportunisticamente (ADR-025).
- `computeWorldMatrix` sempre corrente; `worldMatrix` = cache do último
  `updateWorldTransforms` (stale documentado e testado).
- `updateWorldTransforms` é ITERATIVO — cadeia de 2000 níveis validada.
- Composição validada contra oráculo independente (`Transform::transformPoint`).

## Testes (19 casos / 2147 asserções)

Hierarquia completa, ciclos, cascata, bypass, transforms (translação/rotação/
escala/3 níveis), cache vs sob demanda, múltiplas raízes, órfãos, eachChild
mutável, profundidade 2000, leitura const.

## Links tipados entre entidades (evolução P0-5, ADR-051)

Um link é um registro `(tipo, from, to)` na `LinkRegistry` — relações entre
entidades que NÃO são parent/child (ex.: "target", "follow"). Decisões:

- **Tipado**: tipos registrados antecipadamente com flags (`hierarchical`
  = participa da detecção de ciclos). `create` com tipo não registrado é
  ERRO — não existe "link genérico".
- **Handles estáveis**: `LinkId{index, generation}` com free-list —
  destruir + criar NÃO ressuscita handles antigos (mesma técnica do ECS,
  ADR-024). Operações com handle obsoleto são no-op seguro.
- **Ciclos**: criar link hierárquico `from→to` falha se `to` já alcança
  `from` por links hierárquicos. Ciclos que só fecham por aresta
  direcional são PERMITIDOS por design. Self-link e duplicata exata são
  proibidos.
- **Propagação**: `eachReachable(type, from, fn)` — BFS transitivo
  (paridade com a engine TS legada).
- **Destruição**: `Scene::destroyNode` remove os links de TODAS as
  entidades da subárvore; bypass deixa pontas mortas que `sweepLinks()`
  varre (tolerância oportunista, mesma política da `Hierarchy`).
- **Serialização**: seção opcional `"links"` — tipos persistem no arquivo
  (nada de estado global), pontas por `SceneEntityId`; ponta ausente no
  load é ParseError; ordem de criação preservada (round-trip estável).

## Camadas GAME/SUBGAME/nomeadas (evolução P0-5, ADR-051)

Camadas são ESTRUTURA DE CENA: a `LayerRegistry` é propriedade da `Scene`
e persiste no arquivo. Física, animação, partículas e viewport — que já
recebem `Scene&` — consultam participação sem nova dependência de módulo.

- Built-ins: `GAME` (default — ausência de `LayerMember` = GAME) e
  `SUBGAME` (segundo grupo de simulação independente). Nomeadas via
  `addLayer(name)`.
- **Participação** por camada: `update`/`physics`/`render` (bool).
  Consulta canônica: `Scene::participatesIn(entity, LayerStage)`.
- **timeScale** por camada (0 = pausada): animação e partículas escalam o
  dt POR ENTIDADE (`Scene::timeScaleOf`); física NÃO escala por camada em
  P0-5 (timestep global — corpos em camada sem `physics` ficam estáticos).
- `LayerMember` é COMPONENTE refletido (campo `layer`, default `"GAME"`) —
  registrado built-in no serializer (junto a Transform/Name): aparece no
  Inspector, persiste, clona no Play.
- Remoção de camada em USO é REJEITADA (varredura de `LayerMember`; sem
  fallback silencioso). `LayerMember` referenciando camada ausente no load
  é ParseError.
- Serialização: seção `"layers"` SEMPRE emitida (defaults incluídos);
  arquivos antigos (sem a seção) carregam com GAME/SUBGAME default.
  `formatVersion` PERMANECE 1 (seções aditivas, compatível nos dois
  sentidos).

## Testes (49 casos / 2413 asserções)

Hierarquia completa, ciclos, cascata, bypass, transforms (translação/rotação/
escala/3 níveis), cache vs sob demanda, múltiplas raízes, órfãos, eachChild
mutável, profundidade 2000, leitura const + links (tipos/handles/ciclos/
BFS/sweep/round-trip) e camadas (participação/timeScale/remoção/round-trip/
compat/ParseError).
