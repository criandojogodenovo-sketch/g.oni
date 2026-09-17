# ADR-039 — Android: Activity+JNI e render na UI thread

- **Estado:** aceito (FASE 7, missão §III/§VII)
- **Contexto:** o runtime Android precisa de uma Activity, uma fronteira JNI
  pequena e uma decisão de thread de render que mantenha o lifecycle correto
  sem um sistema multithread complexo (missão §VII).

## Decisão

### Activity padrão (não NativeActivity)

`GoniActivity : Activity` + `SurfaceView` + `SurfaceHolder.Callback2` —
**sem** `NativeActivity`. Motivo: a missão (§III.1) exige NativeActivity
apenas se a auditoria demonstrasse razão técnica — não demonstrou; pelo
contrário:

- lifecycle Java explícito (`onPause/onResume/onDestroy`) mapeia 1:1 no
  runtime nativo, com controle fino do momento de cada chamada JNI;
- SurfaceView entrega a `Surface` (janela nativa via NDK) SEM amarrar o
  loop de eventos ao nativo;
- a fronteira JNI fica mínima e testável (§II.5), em vez do contrato
  implícito de NativeActivity (ANativeActivity callbacks em C, mais
  acoplamento, não menos).

A Activity não contém lógica do engine nem rendering (§III.4/§III.5) —
apenas: inicializar runtime nativo, lifecycle, surface, loop e eventos.

### Render na UI thread via Choreographer (ADR-039 propriamente dito)

O loop de frames é `Choreographer.postFrameCallback` em cadeia, chamando
`nativeRenderFrame` por vsync. O runtime C++ (e portanto o Renderer e os
backends) roda INTEIRO na UI thread:

| Critério | Análise |
|---|---|
| EGL (§VII) | contexto `eglMakeCurrent` na thread que renderiza — UI thread; sem migração de contexto |
| Vulkan (§VII) | todas as chamadas numa única thread — contrato single-threaded (ADR-035) satisfeito por construção |
| Concorrência | ZERO locks: callbacks JNI chegam todos da UI thread |
| Pause | `onPause` para o Choreographer ANTES do `nativeOnPause` — nenhum render após pause |
| Surface | `surfaceDestroyed` (Java) → `nativeSurfaceDestroyed` síncrono na mesma thread do próximo frame — impossível render em surface morta |
| Custo | trabalho de render na UI thread — aceitável para o runtime de demonstração desta fase |

Alternativas rejeitadas: render thread nativa dedicada (mais complexa,
exigiria sincronização explícita de surface/pause/resize entre threads —
a missão §VII pede a solução MAIS SIMPLES correta); `GLSurfaceView`
(empacota um render thread e esconde o lifecycle — controle insuficiente
para o contrato do engine).

## Consequências

- Escala futura (runtime de jogo completo com jobs/simulação pesada): a
  render thread dedicada entra COM o runtime de jogo (FASE 8+), não com o
  lifecycle — decisão registrada para reavaliação.
- `nativeRenderFrame` é idempotente e seguro em qualquer estado do runtime
  (testado no Linux — a mesma função que o APK chama).
