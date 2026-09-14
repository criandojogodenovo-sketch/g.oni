# ADR-025 — Scene: hierarquia como componentes sobre o ECS

- **Estado:** aceito (FASE 2, missão §B.5)
- **Contexto:** a cena precisa de nós com hierarquia (pai/filhos) e
  transforms compostos, sem duplicar entidades/armazenamento próprios.

## Decisão

### Nós = entidades + componentes

`Scene` **compõe** (é dona de) um `eng::ecs::World`. Nó = entidade com:
- `Hierarchy` — pai (`kNoEntity` = raiz; floresta de múltiplas raízes
  permitida) + filhos em **ordem de anexação** (vector).
- `eng::math::Transform` — TRS local (identidade na criação; mutável por
  ponteiro direto).

A hierarquia NÃO é uma estrutura paralela: vive nos sparse-sets do ECS —
herda gratis a semântica de handles geracionais e no-ops seguros.

### Operações de hierarquia

- `attach(child, parent)` — move o filho (desanexa do pai atual);
  **idempotente** para o mesmo pai; devolve false para inválidos,
  self-attach ou **ciclo** (detecção subindo a cadeia de `parent` — a aresta
  proibida é "child ancestral de parent").
- `detach(node)` — vira raiz; false se já raiz.
- `destroyNode(node)` — destrói a subárvore INTEIRA (cascata), folhas
  primeiro (determinístico); desanexa o nó do próprio pai — pai e irmãos
  sobrevivem.
- `eachChild(parent, fn)` — snapshot interno (mutação segura durante a
  iteração), ordem de anexação, pula referências obsoletos.

### Bypass do world (limpeza oportunista)

Destruição DIRETA via `world().destroy(e)` contorna a Scene e deixa
referências obsoletas nas listas de filhos. Política: tais referências são
**puladas** (eachChild/childCount/update/attach) e **apagadas
oportunisticamente** quando o pai sofre qualquer operação de desanexação.
Um nó com pai obsoleto é tratado como RAIZ para transform e ciclo. O
caminho suportado é `destroyNode` — bypass é tolerado, não encorajado.

### Transforms: dois caminhos explícitos

- **Sob demanda:** `computeWorldMatrix(node)` — sobe a cadeia de pais
  compondo `local(pai) * ... * local(nó)` — O(profundidade), SEMPRE
  corrente, sem cache.
- **Em lote:** `updateWorldTransforms()` — recompute iterativo (pilha
  explícita, **sem recursão** — árvores profundas não estouram a pilha) de
  todas as matrizes, top-down, cacheiadas no componente `WorldMatrix`;
  `worldMatrix(node)` lê o cache do último update (nullptr se o nó não era
  coberto). Semântica de stale-cache DOCUMENTADA: sem update, o cache
  envelhece (testado); `computeWorldMatrix` é sempre corrente.

A ordem de composição é a do motor (column-major, vetores-coluna):
`world = worldPai * local` — validada contra o oráculo independente
`Transform::transformPoint` (tests comparam `M·p` com `pai.transformPoint`).

### Integração futura

Eventos de cena (criado/destruído/anexado) via `eng::events` e paralelização
do update via `eng::jobs` são naturais pela composição — NÃO implementados
nesta fase (sem stubs; aguardam consumidores reais e testes próprios).

## Verificações

19 casos / 2147 asserções (ASan+UBSan+Werror): criação; attach/detach/
idempotência/re-anexação; rejeição de inválidos/self/ciclos (árvore intacta);
cascata de destruição; bypass tolerado + limpeza oportunista; translação/
rotação/escala/3 níveis (oráculo transformPoint); cache vs sob demanda
(inclusive stale-cache documentado e nó criado pós-update); múltiplas raízes;
órfãos de bypass como raízes; eachChild com mutação; **cadeia de 2000
níveis** (update iterativo); leitura const.
