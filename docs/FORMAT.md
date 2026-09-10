# Formato .g.oni — Especificação

Extensão: `.g.oni` (ex.: `meu-jogo.g.oni`)

## Camada física

```
[0..3]  4 bytes: cabeçalho mágico "GON1" (0x47 0x4F 0x4E 0x31, little-endian uint32 0x474F4E31)
[4..7]  4 bytes: uint32 LE = tamanho do JSON original (não comprimido)
[8..]   payload: gzip (deflate) do JSON — ou JSON puro quando o navegador
        não suporta CompressionStream
```

Detecção na importação: magic `GON1` → lê cabeçalho e descomprime;
bytes `1F 8B` → gzip puro; senão → JSON puro.

## Estrutura do JSON

```jsonc
{
  "version": "1.0",
  "engine": "G.oni Llumni",
  "name": "Meu Jogo",
  "createdAt": 1699999999999,
  "updatedAt": 1699999999999,

  "scenes": [                        // v1: uma cena (scenes[0] = "main")
    {
      "name": "main",
      "gravity": -9.81,
      "environment": {               // RenderSettings
        "skyZenith": [0.09, 0.13, 0.22, 1],
        "skyHorizon": [0.35, 0.42, 0.52, 1],
        "ambientSky": [0.16, 0.19, 0.26, 1],
        "ambientGround": [0.08, 0.07, 0.06, 1],
        "ambientIntensity": 1.0,
        "exposure": 1.15,
        "bloomStrength": 0.55,
        "bloomThreshold": 1.0,
        "vignette": 0.25,
        "shadowsEnabled": true,
        "postEnabled": true
      },
      "roots": [ /* G.oni Objetos (árvore) */ ]
    }
  ],

  "scripts": [
    { "name": "girar", "source": "# G.oni Script\n..." }
  ],

  "assets": {
    "meshes": {                       // malhas editadas (modelagem)
      "asset:mesh_xxx": { "p": [..], "n": [..], "u": [..], "i": [..] }
    },
    "textures": {                     // dataURL PNG
      "tex_yyy": "data:image/png;base64,..."
    }
  },

  "settings": {
    "render": { /* espelho de environment */ },
    "physics": { "gravity": -9.81 },
    "construt": {                     // prefabs + snap
      "gridSnap": 0.5,
      "prefabs": [ { "name": "Inimigo", "template": { /* objeto serializado */ }, "createdAt": 0 } ]
    },
    "links": { "nodes": [], "edges": [] }   // G.oni Links
  }
}
```

## G.oni Objeto serializado

```jsonc
{
  "id": "obj_12_ab3cd",
  "name": "Cubo",
  "transform": {
    "position": [0, 1.5, 0],
    "rotation": [0, 0, 0],           // radianos, ordem YXZ
    "scale": [1, 1, 1]
  },
  "visible": true,
  "tags": ["inimigo"],
  "metadata": {},                     // dados livres do usuário
  "components": [
    { "type": "mesh",
      "meshId": "prim:cube",          // "prim:<nome>" ou "asset:<id>"
      "material": { "albedo": [0.9, 0.62, 0.1, 1], "metallic": 0.1, "roughness": 0.35, ... },
      "lodDistance": 0, "lodMeshId": null },
    { "type": "collider", "collider": { "shape": "box", "size": [0.5, 0.5, 0.5], "radius": 0.5, "height": 1, "offset": [0, 0, 0], "isTrigger": false } },
    { "type": "rigidbody", "body": { "type": "dynamic", "mass": 1, "restitution": 0.35, ... } },
    { "type": "script", "scriptName": "girar" },
    { "type": "light", "light": { "type": "point", "color": [1,1,1,1], "intensity": 3, "range": 10 } },
    { "type": "camera", "perspective": true, "fovDegrees": 60, "primary": true },
    { "type": "animation", "clips": [ /* AnimationClip */ ], "autoplay": false }
  ],
  "children": [ /* filhos */ ]
}
```

## IDs de malha

| Padrão | Origem |
|---|---|
| `prim:cube` `prim:sphere` `prim:plane` `prim:cylinder` `prim:cone` `prim:capsule` `prim:torus` | primitivas paramétricas (geradas sob demanda) |
| `asset:mesh_xxx` | malhas do editor de modelagem (serializadas em `assets.meshes`) |

## Sistema de arquivos virtual (IndexedDB)

O mesmo conteúdo vive espalhado no VFS para edição incremental:

```
MeuProjeto.g.oni/
├── scenes/main.json      ← scenes[0]
├── scripts/<nome>.goni   ← scripts[]
├── models/<id>.json      ← assets.meshes["asset:<id>"]
├── textures/<id>.json    ← { "dataUrl": "..." }
├── animations/<id>.json  ← clipes soltos (futuro)
└── project.json          ← { name, createdAt, engine, version }
```

## Compatibilidade

- `version` segue semver; loaders aceitam "1.x"
- Campos desconhecidos são ignorados (extensão sem quebra)
- `GOniFormat.importBuffer()` aceita: arquivo com magic, gzip puro ou JSON puro
