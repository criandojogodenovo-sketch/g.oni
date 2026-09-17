# Arquitetura — Physics · Animation · Particles (FASE 10)

> `eng::physics`, `eng::animation`, `eng::particles` — gameplay físico e
> visual sobre o ECS (missão §7; ADR-048). Nenhum conhece RHI/Android.

## eng::physics

```text
RigidBody{mass,velocity,gravity,damping}    ─┐ componentes ECS
Collider{Sphere|AABB,layer,mask,trigger}    ├─ refletidos/serializáveis
CharacterBody{velocity,radius}              ─┘ (catálogo pelo editor)
        ↓ PhysicsWorld::step(scene, fixedDt)   [TimestepAccumulator]
integração semi-implícita → detecção (pares c/ filtro layer&mask)
→ contatos (triggers SEM resolução) → projeção posicional + impulso
raycast(origin,dir,maxD[,mask]) → RaycastHit{hit,entity,point,normal,dist}
moveAndSlide(scene, body, motion) — character body (§7.5)
```

## eng::animation

```text
AnimationClip{position/rotation/scale keys}  → AnimationBank (por nome)
Animator{clip,time,speed,loop,playing,apply*}  → componente ECS
AnimatorStateMachine — GATILHO de transição + vista do estado (regras no
  gameplay; blendDuration, default 0.15s; troca seca quando <= 0)
Animator{previousClip,previousTime,blendDuration,blendRemaining} — o
  estado de cross-fade vive NO COMPONENTE (serializável — correção C-13
  da auditoria final 4–10; antes a transição "estalava")
AnimationSystem::update(scene, bank, dt) — aplica TRS interpolado
  (position/scale lerp · rotation SLERP; pausado APLICA o cursor) e
  COMPÕE as poses do previous e do current durante o fade (o clip que
  sai continua tocando, clamp no fim; t = 1 - blendRemaining/duration)
Skeletal: hierarquia de nós É a preparação (§7.11); skinning = extensão.
```

## eng::particles

```text
ParticleEmitter{rate,lifetime,speed,direction,spread,size,rotation,
                gravity,playing,maxParticles} — componente ECS
ParticlePool{particles,accumulator,spawnIndex} — runtime (não serializa)
ParticleSystem::update(scene, dt):
  integração (v+=g·dt; p+=v·dt) → morte por idade → spawn por acumulador
  direção determinística (van der Corput por índice — sem RNG)
burst(scene, emitter, count) — API de gameplay
```
CPU v1 (§7.13 — decisão ADR-048); o viewport do editor renderiza as
partículas como quads (`Viewport::buildParticleQuads` → ViewportRenderer,
correção do drift D6 da auditoria final 4–10 — antes o emitter simulava
mas nada era desenhado).

## Integração (§8 FASE 10)

- Componentes registrados no catálogo ÚNICO do serializer pelo EDITOR
  (`editor/src/ComponentRegistration.cpp` — engine/scene não depende de
  gameplay); inspector os edita, cena os persiste (round-trip testado).
- Em PLAY o `tick()` do EditorDocument avança input (FASE 9) + física
  (timestep fixo) + animação + partículas SOBRE O CLONE — a edição
  permanece intacta (ADR-044; testes "PLAY avança ... sobre o CLONE").

## Testes (Linux — estado puro)

- `physics`: 15 casos/59 asserções — gravidade/estático/velocity/força/
  impulso; esfera-esfera (contato+normal+depth), trigger sem resolução,
  layers/masks, repouso em AABB, esfera-AABB/AABB-AABB, raycast
  (próximo/ponto/normal/miss/mask/erros), determinismo do
  timestep entre fatiamentos, acumulador anti-espiral, character
  slide/livre + snapToGround (remediação C-18: projeta ao chão quando o
  movimento é horizontal e há chão a meio raio; vertical não snapa).
- `animation`: 6 casos/32 — interpolação lerp/slerp/bordas, playback com
  speed/loop, fim sem loop, pausa+seek, flags de aplicação, máquina de
  estados com cross-fade APLICADO AO NÓ (remediação C-13: a pose do nó é
  a mistura verificável — antes só a utilidade blend() era testada)/no-ops
  seguros.
- `particles`: 6 casos/33 — spawn por rate com dt arbitrário, integração
  com gravidade (semi-implícita), morte por lifetime, burst + pool cheia,
  emitter parado, determinismo bit-a-bit (mesma sequência) + invariância
  de contagem entre fatiamentos.
- `editor` +3 casos — catálogo/round-trip de componentes de gameplay;
  PLAY avança física/animação/partículas no clone e a edição fica intacta.
