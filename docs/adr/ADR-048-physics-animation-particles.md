# ADR-048 — Física/Animação/Partículas próprias, mínimas e testadas

- **Status:** aceito (FASE 10)
- **Contexto:** missão §7 — sistemas fundamentais de gameplay físico e
  visual sobre o ECS, SEM Vulkan/GLES dentro deles. O roadmap antigo
  citava Jolt — SUPERSADO pela missão atual (primeiro sistema próprio).
- **Decisões:**
  1. **Física própria (D1):** esfera + AABB, integração semi-implícita,
     resolução por projeção posicional proporcional às massas inversas +
     impulso escalar (restituição 0 — gameplay mobile), mass=0 = estático,
     sem inércia angular (documentado). Broad-phase n² (cenas de editor);
     OBB/contínua/joints são extensões.
  2. **Timestep fixo (§7.6):** `TimestepAccumulator` converte o dt do
     frame em N passos de 1/60 (clamp anti-espiral de 8 passos).
     Determinismo testado entre fatiamentos.
  3. **Colisões/triggers/layers (§7.2):** contatos expostos
     (`contacts()`), `isTrigger` = contato SEM resolução, filtro
     `layer & mask` bidirecional.
  4. **Raycast (§7.4):** esfera (raio) e AABB (slab), o MAIS PRÓXIMO
     vence, mask opcional; normal por eixo dominante na AABB
     (aproximação documentada).
  5. **CharacterBody (§7.5):** move-e-desliza de esfera em UMA passada —
     a projeção para fora da superfície absorve a componente normal do
     movimento. Multi-hit re-slide é extensão.
  6. **Animação:** clips TRS (position/scale lerp, rotation SLERP),
     Animator componente (play/pause/stop-natural/loop/speed/seek —
     pausado ainda APLICA a pose do cursor), máquina de estados com
     cross-fade linear (regras ficam no gameplay — §9). SKELETAL (§7.11):
     a hierarquia de nós é a preparação; skinning/mesh é extensão futura
     DOCUMENTADA (sem mesh renderer no engine ainda).
  7. **Partículas CPU (§7.13):** decisão REGISTRADA — GPU exige
     compute/texturas que o RHI não tem; spawn por acumulador de rate
     (independente do dt), direção por sequência de van der Corput
     (determinística — §7.14), pool runtime NÃO serializada, morte por
     idade. A ordem interna é integração→morte→spawn (partícula não
     envelhece o dt que a gerou — bug pego pelo teste de determinismo).
  8. **Integração editor (§8):** componentes refletidos + registrados no
     catálogo do serializer pelo CONSUMIDOR (editor) — engine/scene não
     depende das camadas de gameplay; em PLAY o tick avança física
     (timestep fixo), animação e partículas SOBRE O CLONE (ADR-044).
- **Consequências:** 26/26 suites (physics 14/55, animation 6/30,
  particles 6/33, editor +3 casos de integração); APIs C++ prontas para
  NI-Script (§9/§18); limitações documentadas sem fingir escopo.
