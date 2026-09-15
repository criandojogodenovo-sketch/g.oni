# ADR-033 — Serialização de Scene/ECS: SceneEntityId, reflect-by-name, determinismo

- **Estado:** aceito (FASE 3, missão §2.7)
- **Contexto:** o maior risco técnico da fase — Entity{index,generation} é
  handle de RUNTIME; gravá-lo seria frágil entre builds.

## Decisão

### SceneEntityId (identidade persistente)

UUIDv4 forte sobre `core::Uuid128` (distinto de AssetId/ProjectId —
ADR-028). Atribuído na PRIMEIRA serialização de cada nó (componente
`SceneIdentity` emplantado — efeito visível, testado: id estável entre
saves). No load, entidades são reconstruídas e o mapa
SceneEntityId→Entity resolve parent/child e referências internas de
componentes — handles de runtime NUNCA atravessam o disco.

### Onde o serializer vive (desvio D3)

DENTRO de eng::scene, como arquivos novos (`SceneIdentity.hpp`,
`SceneSerializer.hpp/.cpp`). Motivação: a regra dura "nenhum módulo depende
de scene exceto testes em tests/" (missão §5.1) + a entrega ser "5 módulos
novos + serialização de Scene/ECS" (não um 6º módulo). Arestas novas
PRIVATE: serial, reflect, log — todas abaixo de scene no grafo. Nenhum
arquivo/semântica/teste de FASE 2 alterado (critério D preservado: a suíte
de scene da FASE 2 continua verde e intocada).

### Enumeração: registro explícito de componentes (achado crítico 1)

`World` não expõe enumeração dinâmica de pools (chaves TypeTag opacas —
sem RTTI, ADR-024). O serializer mantém registro EXPLÍCITO de tipos
serializáveis — `registerComponentType<T>(typeName)` com funções tipadas
sobre a API pública (`has/get/emplace`) — e os DADOS vêm do reflect
(PropertyInfo offset + typeName), conforme a missão exige ("por nome via
reflect, nunca por typeid/índice"). Built-in: `eng::math::Transform`
(registrado no carregamento do módulo). A enumeração de NÓS usa
`world().each<Hierarchy>()` (todo nó tem Hierarchy — achado crítico 2);
entidades criadas por bypass no world (sem Hierarchy) são runtime-only.

### Tipos math no reflect

`eng::math::Vec3/Quat/Transform` registrados por ENG_REFLECT no
SceneSerializer.cpp com nomes QUALIFICADOS (chaves estáveis). Extensão de
reflect: NENHUMA — PropertyInfo por offset + nomes canônicos existentes
são suficientes (conclusão da auditoria §1.5 confirmada pelo uso).

### Codec de campo por nome de tipo (extensão em eng::serial)

Componentes referenciam ASSETS e ENTIDADES. Para serializar
`AssetId`/`SceneEntityId` como STRING UUID — sem aresta scene→assets
(proibida pelo layering) — eng::serial ganhou `registerFieldTypeCodec(
typeName, {encode, decode})`: quem conhece o tipo registra o codec no SEU
módulo (`eng::assets/AssetSerial.hpp`, `SceneIdentity.hpp`), registro
INLINE idempotente em header (mesma estratégia de ENG_REFLECT). O codec
SOMBREIA a resolução por reflect para o nome. A forma NULA
("00000000-…-000000000000") é aceita como "sem referência" (identidade
ausente é legítima; `Uuid128::fromString` puro permanece estrito v4).

### Formato (formatVersion 1) e determinismo

```
{"entities":[{"components":[{"data":{…},"type":"eng::math::Transform"}],
  "id":"uuid","parent":"uuid"|null} …],
 "formatVersion":1,"sceneEntityIds":["uuid" …]}
```

- Entidades ordenadas por SceneEntityId (hi,lo); componentes por nome de
  tipo (std::map); chaves ordenadas pelo dump (ADR-030) →
  **serialize(deserialize(x)) == x byte a byte** (testado em todos os
  round-trips, incluindo floats via shortest-repr).
- `sceneEntityIds` = lista canônica; consistência com `entities`
  validada no load (divergência → ParseError).
- `Hierarchy` vira o campo `parent`; `WorldMatrix` não é persistido
  (cache derivado); entradas SceneIdentity/Hierarchy/WorldMatrix no array
  de componentes são TOLERADAS com WARN (escritores externos).
- Pai obsoleto (bypass do world na origem) → serializado como RAIZ
  (política ADR-025).
- **Ordem de anexação NÃO é persistida** — o formato não a codifica;
  pós-load, a ordem dos filhos é a canônica por SceneEntityId. Perda
  documentada e aceita (o round-trip byte-idêntico depende exatamente
  disto: a ordem derivada é função do conteúdo).

### Tolerâncias e erros

- Load: erros claros para formato futuro (NotSupported), uuid inválido,
  componente desconhecido (NotSupported com o nome), parent ausente,
  ciclo, id duplicado, sceneEntityIds divergente — NUNCA throw/abort;
- Referência de ASSET quebrada NÃO impede o load: o parse valida FORMA;
  a checagem contra o AssetRegistry pertence à camada de COMPOSIÇÃO
  (runtime/editor/tests — quem possui scene+assets), que reporta via
  eng::log. Demonstrado no teste com CapturingSink (desvio D4).
- load ADICIONA nós aos existentes (merge por id: duplicado → erro).

### Thread-safety (ADR-034)

save/load não são concorrentes sobre a mesma Scene; registro de
componentes em init single-threaded, leitura concorrente segura depois;
o registro de codecs de campo é protegido (escrita exclusiva, leitura
compartilhada — mesma política do TypeRegistry).

## Alternativas rejeitadas

- Serializar Entity{index,generation}: frágil entre builds (motivo de
  existir o SceneEntityId);
- Estender ecs com enumeração de pools: alteraria FASE 2 por uma
  necessidade da FASE 3 — o registro explícito resolve sem tocá-la;
- Serializer em eng::serial ou eng::assets: criaria aresta → scene,
  proibida por §5.1;
- Persistir ordem de anexação: quebraria o round-trip canônico e
  adicionaria estado redundante ao formato.
