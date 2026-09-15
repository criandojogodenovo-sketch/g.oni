# Serialização de Scene/ECS (FASE 3, dentro de eng::scene)

> Identidade persistente, reflect-by-name e determinismo byte-a-byte.
> Decisões completas: [ADR-033](../adr/ADR-033-scene-serialization.md).

## Posição no grafo

```
eng::scene ──▶ eng::serial, eng::reflect, eng::log (PRIVATE, arquivos novos)
```

Vive DENTRO de eng::scene porque "nenhum módulo depende de scene exceto
tests/" (§5.1) — desvio D3 da auditoria. Nenhum arquivo/semântica de
FASE 2 alterado.

## API essencial

```cpp
// Componente de gameplay registrado (nome = chave estável do reflect):
ENG_REFLECT_BEGIN(SpriteRef)
    ENG_REFLECT_FIELD_AS(texture, "eng::assets::AssetId") // → string UUID
    ENG_REFLECT_FIELD(opacity)
ENG_REFLECT_END()
eng::scene::SceneSerializer::registerComponentType<SpriteRef>("SpriteRef");
// Built-in já registrado pelo módulo: eng::math::Transform

auto text = eng::scene::SceneSerializer::save(scene).value();
// EFEITO: atribui SceneEntityId (UUIDv4) a nós sem identidade.

eng::scene::Scene clone;
eng::scene::SceneSerializer::load(clone, text); // anexa à cena existente
```

## Modelo

- `SceneEntityId` (UUIDv4 forte) no componente `SceneIdentity` — handles
  `{index,generation}` NUNCA atravessam o disco;
- Formato: entidades ordenadas por id, componentes por nome, `parent`
  por identidade, `sceneEntityIds` validado contra `entities`;
- Componentes por registro EXPLÍCITO (World não enumera pools — achado
  crítico 1); dados via reflect (offset + typeName), enums por NOME;
- Referências AssetId/SceneEntityId em campos → codecs de campo do
  `eng::serial` (string UUID; forma nula = "sem referência");
- Hierarchy → campo `parent` (ordem de anexação não é persistida);
  WorldMatrix não persistido; internos no JSON → WARN e tolerados;
- Referência de asset quebrada NÃO impede o load — checagem é da camada
  de composição (demonstrado com CapturingSink nos testes).

## Invariantes testadas

- `serialize(deserialize(x)) == x` byte a byte (vazia, solitária,
  hierarquia, componente com AssetId/SceneEntityId, pós-mutação);
- erros claros: formato futuro, uuid ruim, componente desconhecido,
  parent ausente, ciclo, id duplicado, divergência de lista;
- id estável entre saves consecutivos.
