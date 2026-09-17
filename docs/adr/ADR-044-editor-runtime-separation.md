# ADR-044 — Separação editor × runtime (PLAY/STOP por clone)

- **Status:** aceito (FASE 8)
- **Contexto:** missão §8.7 — o estado do editor DEVE ser separado do
  estado do runtime; editor → cena → runtime → RHI sem misturar.
- **Decisão:** `EditorDocument` mantém a cena em EDIÇÃO como única fonte
  da verdade. `play()` cria o runtime por **clone de serialização**:
  `SceneSerializer::save(edicao)` → `load(nova Scene)` (round-trip
  determinístico já testado na FASE 3 — ADR-033). `stop()` DESCARTA o
  clone. Nenhum ponteiro compartilhado entre os dois estados.
- **Contrato em Play:**
  - Comandos de EDIÇÃO (create/delete/rename/reparent/setTransform/
    setField/addComponent/removeComponent/save/load) retornam
    `StatusCode::InvalidState` ("edição rejeitada: editor em PLAY");
  - Consultas (hierarquia, inspector, transform) leem o CLONE —
    ferramenta de debug do estado do runtime;
  - Arrastar entidade em Play altera o CLONE (debug §8.7) — o teste
    "mutação em Play não vaza para edição" garante a separação;
  - O segundo `play()` re-clona do estado atual da edição.
- **Threading:** inalterado (UI thread + Choreographer — ADR-039; §11 da
  missão FASE 8–10 exige auditoria antes de mexer: nada mudou).
- **Evolução:** o `tick(dt)` do documento é o ponto de entrada do loop de
  runtime — FASE 9 adiciona input/audio em Play, FASE 10 physics/animation/
  particles. Sem comportamento inventado na FASE 8 (§13).
- **Consequências:** separação por construção (impossível vazar estado por
  ponteiro); custo = serialização no play ( desprezível na escala de cena
  de editor); limitação: mudanças no clone não voltam para a edição (por
  design — SAVE do runtime é fase futura de gameplay real com NI-Script).
