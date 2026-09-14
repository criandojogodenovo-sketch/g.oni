# ADR-024 — ECS: sparse-sets, handles geracionais e iteração por snapshot

- **Estado:** aceito (FASE 2, missão §B.4)
- **Contexto:** a cena (§B.5) e sistemas de gameplay precisam de armazenamento
  de componentes com remoção/destruição frequentes, detecção de handles
  obsoletos e iteração multi-componente.

## Decisão

### Handles: `Entity = { index, generation }`

- Slots de entidades em vector contíguo; índices destruídos vão para uma
  free-list e são reciclados.
- **A geração do slot incrementa a cada destruição**: o handle reciclado tem
  geração diferente — comparação completa do Entity detecta obsolescência em
  todas as operações. Wrap após 2³² destruições do MESMO slot: aceito e
  documentado (prática comum; mitigação futura se provável).
- Operações com handle obsoleto são **no-op seguro** (missão §B.4):
  `destroy`/`remove` → false; `get`/`emplace` → nullptr; `has` → false.
  Nenhuma operação aborta.

### Storage: sparse-set (por tipo de componente)

Escolha entre sparse-set e arquétipo documentada aqui: **sparse-set**.
Motivos: implementação verificável em FASE 2 (sem group management),
iteração packed cache-friendly por tipo, remoção O(1) amortizado, e a API
pública (`emplace/remove/get/has/each`) **não expõe a estrutura interna** —
uma futura migração para arquétipos (quando consultas multi-tipo dominarem o
perfil) não reescreve os call sites.

- `dense_` (componentes packed) + `entities_` (dono por posição, paralelo) +
  `sparse_` (índice → posição; kNpos = ausente; crescido sob demanda).
- Chave por tipo sem RTTI: endereço de membro estático por instanciação
  (mesma técnica de ADR-022).
- Remoção: **swap-and-pop com move-CONSTRUCT** — `T` precisa ser apenas
  move-constructible (move-assign não é exigido); `noexcept` no move é
  recomendado (não exigido — documentado no header).
- `emplace` em entidade que já tem o componente: **substitui**
  (destroy + construct in place) — devolve o ponteiro do novo valor.

### `each<Ts...>`: snapshot do menor pool

- Candidatas = entidades do **menor pool** entre Ts (menor trabalho de
  verificação); para cada candidata, todos os Ts são consultados — só
  entidades com TODOS visitam `fn(Entity, Ts&...)`.
- **Ordem determinística**: inserção no menor pool.
- **Const-correctness**: `World` mutável entrega `Ts&...`; `const World`
  entrega `const Ts&...` (sobrecargas distintas, sem const_cast).
- **Snapshot** (cópia das candidatas no início): mutação durante a iteração é
  SEGURA e definida —
  - destruir a entidade corrente ou outra → a destruída não é mais visitada
    (verificação por ponteiro nulo/válido);
  - remover componentes → a entidade perde a visita subsequente se faltar
    algum Ts;
  - criar entidades/componentes novos → não aparecem NA rodada corrente
    (análogo à política de rodadas do `eng::events`, ADR-022);
  - **nenhuma entidade é visitada duas vezes** (snapshot sem duplicatas).
  Custo: O(m) cópias de Entity (m = menor pool) por chamada. Evolução
  documentada: iteração backward zero-copy (swap-and-pop é backward-safe
  para a entidade corrente, mas remoção de futuras duplicaria visitas) —
  só adotada quando o perfil exigir, com semântica de mutação re-documentada.

### Sem reflect no código

A aresta `ecs → reflect` (missão §B.0) é declarada no CMake para a camada,
mas nenhum símbolo de reflect é consumido (missão §B.4: sem dependência
forçada). Integração futura (auto-registro de componentes para serialização)
usará a API pública de `eng::reflect` sem alterar o storage.

## Verificações

18 casos / 1602 asserções (ASan+UBSan+Werror): ciclo de vida, reciclagem com
geração, no-ops de handle obsoleto (todas as operações), emplace/replace,
move-only components, each single/multi/const, ordem, mutação durante each
(destruir corrente, destruir futura, criar durante, remover componente
paralelo), estresse 1000 entidades com reciclagem.

## Alternativas consideradas

- **Arquétipos (archetypes)** — adiados: gerenciamento de grupos migra
  componentes entre tabelas (complexidade alta para FASE 2); benefício real
  só em consultas multi-tipo densas. API pública já compatível.
- **Pool único type-erased** — rejeitado: perde tipagem estática nos
  acessos e impõe indireção por componente.
- **Iteração backward zero-copy** — documentada como evolução (acima).
