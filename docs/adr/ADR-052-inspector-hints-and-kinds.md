# ADR-052 — Hints de edição no reflect e kinds semânticos no Inspector

- **Status**: ACEITO
- **Data**: 2026-09-18 (evolução P0-6)
- **Contexto**: `docs/goni_engine_audit_current.md` §5.13/§7-P0.6

## Contexto

A auditoria PHASE 0 (§7-P0.6) exige Inspector e Asset Browser com editores
REAIS: "campos por categoria, enums/bools/cores reais, busca; browser com
thumbnails/metadados/preview". O que existia até a P0-5:

1. **O Inspector era cego semanticamente** — `Inspector::Field` carregava
   apenas `{path, typeName, value}`; o host Android renderizava TODOS os
   campos como `EditText` livre. Um `bool` era digitado como "true"/"false";
   um enum como o NOME do enumerador; o tint do sprite como três floats
   soltos (`tintR`, `tintG`, `tintB`); a textura do sprite era um caso
   ESPECIAL hard-coded no Kotlin (`component == "eng::editor::SpriteData"
   && path == "textureAsset"`) — exatamente o que a missão §8.4 proíbe.
2. **O reflect não transportava intenção de edição** — `PropertyInfo`
   descrevia nome/offset/tipo, nada sobre COMO o campo deve ser editado.

Um bug latente descoberto DURANTE a implementação confirmou a gravidade:
`eng::physics::ColliderShape` estava registrado com `ENG_REFLECT_BEGIN`
(macro de STRUCT) em vez de `ENG_REFLECT_ENUM_BEGIN` — o enum virava um
"struct sem propriedades", o campo `shape` do Collider NUNCA apareceu no
Inspector e NUNCA foi serializado (recursão em struct vazia = silêncio
desde a FASE 10).

## Decisão

### 1. Hint de edição no `eng::reflect`

`PropertyDesc`/`PropertyInfo` ganham um campo `hint` (string livre, default
`""`), populado pelos macros:

```
ENG_REFLECT_FIELD_HINT(member, Hint)          // tipo automático + hint
ENG_REFLECT_FIELD_AS_HINT(member, TypeName, Hint)
```

Convenção de hints (documentada, NÃO validada pelo registry — o registry só
transporta):

| Hint | Significado | Consumidor |
|---|---|---|
| `texture` | string que referencia asset de textura do projeto | Inspector → kind `texture` |
| `color:<grupo>:<canal>` | canal r/g/b/a de um grupo de cor | Inspector → kind `color` |

O hint NÃO altera serialização nem layout — é metadado puro de UI. Campos
sem hint comportam-se exatamente como antes (compatibilidade total).

### 2. Kind semântico no `Inspector::Field`

`Field` passa a carregar `{path, typeName, value, kind, options}`:

- `kind` deriva do TIPO refletido + hint — **nunca do nome do campo**
  (§8.4 permanece: zero hard-code de componentes no Inspector):
  - enum → `enum` (com `options` = enumeradores unidos por `|`)
  - bool → `bool`; f32/f64 → `number`; i8..u64 → `int`
  - string → `text` (ou `texture` quando hint = `texture`)
- `color` é sintético: canais CONSECUTIVOS com hints
  `color:<grupo>:<r|g|b|a>` colapsam em UM campo:
  - `path` = membros por vírgula na ordem r,g,b[,a]
    (ex.: `tintR,tintG,tintB`);
  - `value` = `#RRGGBB` ou `#RRGGBBAA`;
  - `setField`/`getField` aceitam o path comma-junto — a escrita valida
    TUDO antes de tocar em qualquer canal (sem escrita parcial);
  - grupo incompleto (falta canal, duplicado, tipo ≠ f32) degrada para
    campos `number` individuais com WARN — nunca quebra a UI.

O TSV JNI `nativeEditorComponentFields` passa a 5 colunas:
`path\ttype\tvalue\tkind\toptions` (o host Kotlin trata colunas ausentes
como `text`/vazio — retrocompatível).

### 3. Host Android renderiza editores por kind

`EditorActivity.addFieldRow` despacha no kind: `Switch` (bool), diálogo de
seleção única (enum), swatch + diálogo RGBA com sliders/preview/hex (color),
picker de textura com thumbnails (texture), EditText numérico/texto (resto).
Nomes de exibição são prettificados (`eng::editor::SpriteData` → "Sprite";
a CHAMADA de API continua com o nome cru). Busca em adicionar-componente e
no browser de assets; thumbnails reais (decode com `inSampleSize` + cache)
nas linhas e no picker.

## Alternativas rejeitadas

- **Hard-code de campos especiais no Kotlin** (estado anterior): viola §8.4,
  não escala, e o caso SpriteData.textureAsset provou o problema.
- **Mudar SpriteData para Vec4 tint**: quebraria o formato de cena salvo
  (ADR-043: campos por caminho) e exigiria migration para ganho zero — o
  hint agrupa canais em runtime sem tocar em disco.
- **Hint validado/enumerado no registry**: o registry de tipos é camada de
  MOTOR; intenção de edição é camada de editor. Manter string livre mantém
  o grafo de dependências e permite novos hints (material, mesh, audio…)
  sem mudar eng::reflect.

## Consequências

- O bug latente do ColliderShape foi CORRIGIDO (macro correto); cenas novas
  passam a serializar `shape` por nome ("Sphere"/"Box" — ADR-033); cenas
  antigas continuam carregando (campo ausente → default Sphere, tolerância
  existente do decoder).
- `setField` em grupo de cor escreve canais individuais — o snapshot de
  undo/clone do EditorDocument continua operando por componente (ADR-044),
  sem alteração.
- Novos componentes ganham editores ricos AUTOMATICAMENTE ao registrarem
  hints (nenhum código de UI novo por componente).
- Caminhos comma-juntos existem APENAS em Field sintético de cor — não são
  aceitos em serialização (StructCodec não conhece o Inspector).

## Evidências

- `engine/reflect/tests/ReflectTests.cpp`: hints trafegam no PropertyInfo
  (macro + API direta), 15 casos/100 asserções verdes.
- `editor/tests/EditorTests.cpp`: 5 casos novos (enum kind+options,
  bool/number/int/text, colapso de cor com round-trip hex↔floats e clamp,
  rejeição de hex lixo SEM escrita parcial, kind texture) — 46 casos/501
  asserções verdes.
