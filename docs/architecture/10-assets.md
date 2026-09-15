# eng::assets — Identidade, Catálogo e Ciclo de Vida (FASE 3)

> AssetId UUIDv4 estável, registry persistente, resolver com anti-traversal
> e cache single-threaded. Identidade: [ADR-028](../adr/ADR-028-asset-identity.md)
> · ciclo/escopo: [ADR-029](../adr/ADR-029-asset-lifecycle.md).

## Posição no grafo

```
eng::core, eng::fs, eng::serial, eng::reflect ──▶ eng::assets
                                                  ◀── eng::events (declarada, FASE 4)
```

`jobs` é PROIBIDO nesta fase (missão §2.10/R14) — nenhum async existe.

## API essencial

```cpp
const auto id = eng::assets::AssetId::generate(); // UUIDv4 forte

eng::assets::AssetRegistry registry;
registry.upsert({id, eng::assets::AssetType::Scene,
                 eng::fs::Path{"scenes/main.json"}});
registry.serialize();                       // asset_registry.json determinístico
eng::assets::AssetRegistry::deserialize(text);

eng::assets::AssetResolver resolver(registry, paths.assetsRoot());
resolver.resolve(id);   // rejeita absoluto e traversal (isWithin)
resolver.read(id, fs);  // → bytes

eng::assets::AssetManager manager(registry, resolver, fs);
manager.registerLoader<eng::serial::JsonValue>(
    std::make_shared<eng::assets::JsonAssetLoader>());
auto handle = manager.load<eng::serial::JsonValue>(id).value();
manager.getLoaded<eng::serial::JsonValue>(id); // optional; guarda de tipo
manager.unload(id);                            // handles vivos sobrevivem
```

## Modelo

- **Tipos**: Scene/Prefab/Json implementados; Texture/Mesh/Material/
  Shader/Audio/Script RESERVADOS sem loader (load → NotSupported).
- **source/cache/runtime**: conceitual — source é o JSON editável do
  registry; cache é derivado (nada cookado na FASE 3; envelope binário é
  o formato futuro); runtime é o cache do manager + handles.
- **Renomear** = upsert com novo sourcePath; id (e referências) ficam.
- Loaders type-erased por TypeTag com invocadores por chave — sem RTTI.

## Testes

100k gerações sem colisão (conjunto real); round-trips string/bytes/registry;
renomeação preserva referências; traversal/absoluto rejeitados com
InvalidArgument; cache/unload/getLoaded com guarda de tipo; tipo reservado
→ NotSupported com nome do tipo.
