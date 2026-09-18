# Arquitetura — Editor (FASE 8)

> Consumidor de `engine/` (regra 00-overview): `editor/` (C++) e
> `android/app` (Kotlin/JNI) NUNCA são incluídos pelo engine.

## Visão

```text
EditorActivity + EditorJni.kt (Kotlin — chrome touch, SEM lógica de engine)
        ↓ JNI (EditorJni.cpp — 2º TU com jni.h; TSV snapshots, handles Long)
EditorHost (C++) — surface/lifecycle (ADR-039/040) + documento
        ↓
EditorDocument — ESTADO + COMANDOS (§8.9: Project/Scene/Selection/Viewport/Mode)
   ├── Inspector      → reflect: campos por offset ↔ string (ADR-043)
   ├── AssetBrowser   → fs + AssetRegistry compostos (§8.5)
   ├── Viewport       → câmera 2D + hit-test + quads (world matrix)
   └── [Play] runtimeScene = clone por SceneSerializer (ADR-044)
        ↓
eng::scene / eng::serial / eng::project / eng::assets / eng::fs / eng::reflect
        ↓
eng::rhi (Renderer/Frame) → backends Vulkan/GLES (FASES 5–7, intocados)
        ↓
ViewportRenderer — pipeline pos+cor, VBO dinâmico CPU→clip (ADR-042)
```

## Módulo `editor/`

| Peça | Papel |
|---|---|
| `EditorDocument` | projeto (new/open/save/settings), cena (new/save/load), entidades (create/delete/duplicate/rename/reparent), TRS com Euler em graus, seleção, play/stop, viewport, snapshot de hierarquia, pack/unpack JNI; **scripts .nis como assets (P0-7, ADR-053)**: list/read/write/create/delete/compile-check/assign |
| `Inspector` | catálogo `componentEntries()` (único — ADR-043); campos por caminho `position.x`; enums por nome; escrita validada por tipo; **kinds semânticos + hints (P0-6, ADR-052)**: `{path, typeName, value, kind, options}` — enum/bool/number/int/text/texture/color com grupo de canais colapsado em hex |
| `AssetBrowser` | categorias→AssetType; registry ∪ varredura de disco; import (staging→`assets/<cat>/`+upsert), rename/move/delete com AssetId estável; **registerExisting (P0-7)**: cataloga arquivo já posicionado (scripts são escritos direto) |
| `Viewport` | mundo Y-cima ↔ tela Y-baixo (Android); pan/zoom com foco; quads depth-first; hit-test top-most com raio de toque |
| `ViewportRenderer` | shaders pos+cor (cópia exata dos fixtures FASES 5–7 — regeneração por script); grade/entidades/bordas (seleção branca, play verde); `updateBuffer` por frame |
| `EditorHost` | state machine de surface idêntica à FASE 7 (NoSurface/Available/ChangedPending/Destroyed + paused); ANativeWindow ownership (acquire/release, renderer morre antes — ADR-040) |

## Estados

```text
Mode::Edit  ──play()──▶  runtimeScene = clone(save(scene_))  ──Mode::Play
Mode::Play  ──stop()──▶  runtimeScene descartado              ──Mode::Edit
```

Em Play: edição rejeitada (`InvalidState`), consultas leem o clone,
arraste muta o clone (debug), stop preserva a edição intacta (ADR-044).

## Testes (Linux, backends reais)

50 casos / 566 asserções (evolução P0-7: + scripts como assets — create com
Template que COMPILA (provado), write/read round-trip com id estável,
compile válido×inválido com diagnósticos, assign→PLAY→tick com efeito;
evolução P0-6: + kinds enum/bool/color/texture, colapso de cor com
round-trip hex e clamp, rejeição de hex sem escrita parcial, hint de
textura): projeto, entidades, hierarquia, componentes, inspector
(get/set/erros/protegidos/kinds), Euler round-trip, save/load de cena,
PLAY/STOP, câmera/hit-test/world matrix, assets (import/list/rename/move/
delete/não-catalogado/registerExisting), EditorHost×{GLES,Vulkan} com
ciclos de surface/pause, pack/unpack JNI.

## UI Android por kind (P0-6, ADR-052)

O host renderiza o editor adequado por kind — nada de digitar
"true"/"Sphere"/hex à mão: `Switch` (bool), diálogo de seleção única
(enum, options do C++), swatch + diálogo RGBA com sliders/preview/hex
(color), picker de textura com thumbnails (texture), EditText
numérico/texto (resto). Busca em adicionar-componente e no browser de
assets; thumbnails reais (inSampleSize + cache) nas linhas do browser e
no picker. Nomes de exibição prettificados ("Sprite", "Collider") — as
chamadas JNI continuam com o nome canônico cru.

## Painel Scripts (P0-7, ADR-053)

Barra inferior: lista dos .nis do projeto + "Novo script" (template
válido). Toque abre o editor: fonte multi-linha monospace, **Compilar**
(diagnósticos line:col ou confirmação — mesma tabela de nativos do
Play), **Anexar** à entidade selecionada, **Salvar** (write multi-KB
via jniToString — sem o limite de 512B do copyJString). O asset é a
fonte de EDIÇÃO; a cena carrega cópia estável (autocontida).

## Limitações v1 (honestas)

- Viewport 2D (marcadores de entidade — sem meshes/materiais no engine
  ainda); transform por Euler XYZ (gimbal ±90° com tolerância);
- sem undo/redo (fase futura); sem multi-seleção;
- "open project" lista apenas projetos locais (SAF para import de asset);
- APK: BUILD/PACKAGED/INSPECTED — sem execução em dispositivo (evidência
  por estágio, §13).
