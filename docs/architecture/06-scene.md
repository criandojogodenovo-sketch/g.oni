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
