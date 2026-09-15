# Layout de projeto (esperado, não imposto)

> FASE 3, ADR-032. A engine impõe APENAS: o arquivo `project.goni.json`
> existe e os paths dentro dele são relativos ao SEU diretório. Todo o
> resto é convenção recomendada — documentada aqui para editores e
> ferramentas terem uma base comum.

## Exemplo mínimo

```
meu-jogo/
├── project.goni.json        # raiz lógica do projeto (formatVersion 1)
├── asset_registry.json      # catálogo id → meta (path declarado no config)
├── assets/                  # raiz convencional: sourcePaths do registry
│   ├── scenes/
│   │   └── main.goni.scene.json
│   └── menus/
│       └── hud.goni.scene.json
├── cache/                   # derivado, regenerável (vazio na FASE 3)
└── build/                   # saídas de build (não pertence à engine)
```

## project.goni.json (canônico)

```json
{
    "assetRegistryPath": "asset_registry.json",
    "engineVersion": "0.3.0",
    "formatVersion": 1,
    "name": "Meu Jogo",
    "projectId": "00112233-4455-4677-8899-aabbccddeeff",
    "sceneRoots": ["assets/scenes", "assets/menus"]
}
```

Regras válidas pelo PARSE (erros claros, ADR-032): `formatVersion` maior
que a suportada → rejeitado; `projectId` UUIDv4 canônico; qualquer path
ABSOLUTO → rejeitado. Nenhuma string absoluta é persistida em nenhum
arquivo do projeto — mover o diretório inteiro não invalida nada.

## asset_registry.json (canônico)

```json
{
    "assets": [
        {
            "id": "00112233-4455-4677-8899-aabbccddeeff",
            "sourcePath": "scenes/main.goni.scene.json",
            "type": "Scene"
        }
    ],
    "formatVersion": 1
}
```

`sourcePath` é relativo a `assets/` (resolvido por `ProjectPaths::
assetsRoot()`); entradas ordenadas por id; renomear um asset é atualizar o
`sourcePath` — o `id` (e todas as referências) permanecem (ADR-028/029).

## Cena (canônica)

```json
{
    "entities": [
        {
            "components": [
                {
                    "data": {
                        "position": {"x": 0.0, "y": 0.0, "z": 0.0},
                        "rotation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
                        "scale": {"x": 1.0, "y": 1.0, "z": 1.0}
                    },
                    "type": "eng::math::Transform"
                }
            ],
            "id": "00112233-4455-4677-8899-aabbccddeeff",
            "parent": null
        }
    ],
    "formatVersion": 1,
    "sceneEntityIds": ["00112233-4455-4677-8899-aabbccddeeff"]
}
```

Entidades ordenadas por `SceneEntityId`; componentes por nome de tipo;
round-trip byte-estável (ADR-033).

## O que NÃO existe na FASE 3

- Diretório `imported/` ou `runtime/` — a separação SOURCE/IMPORTED/
  RUNTIME é conceitual: source (editável) e cache (derivado, vazio)
  existem; runtime vive só em memória (ADR-029).
- Cook/importação: texturas, meshes e shaders têm TIPOS reservados no
  `AssetType` mas nenhum loader — carregá-los devolve `NotSupported`
  claro.
