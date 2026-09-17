# ADR-040 — Android: ownership de surface/ANativeWindow

- **Estado:** aceito (FASE 7, missão §IV/§V/§VI/§IX/§XII)
- **Contexto:** `ANativeWindow`, `VkSurfaceKHR` e `EGLSurface` têm lifetimes
  distintos e a ordem real dos callbacks Android varia (missão §VI). O
  erro clássico é usar/dupla-liberar a janela após `surfaceDestroyed`.

## Decisão

### Ownership única e explícita

```
SurfaceView (Java)
   ↓ surfaceCreated — JNI: ANativeWindow_fromSurface (ADQUIRI a ref)
AndroidRuntime (C++)  ← ÚNICO owner entre created→destroyed
   ↓ surfaceCreated(window) / surfaceChanged(w,h) / surfaceDestroyed
Renderer (eng::rhi)   ← criado DEPOIS da janela, destruído ANTES dela
   ↓ NativeWindowHandle{ptr, Android} — opaca na abstraction (§II.6)
Backends              ← VkAndroidSurfaceKHR / eglCreateWindowSurface
```

Regras (todas testadas no Linux com o mesmo código do APK):

1. **acquire**: `ANativeWindow_fromSurface` (JNI) já adquire; o runtime
   faz um `ANativeWindow_acquire` adicional ao aceitar a janela — a
   responsabilidade de liberar é DELE e só dele.
2. **release**: `ANativeWindow_release` ocorre em `surfaceDestroyed`,
   RIGOROSAMENTE APÓS a destruição do Renderer (que derruba
   VkSurfaceKHR/VkSwapchain ou EGLSurface/EGLContext) — ordem
   renderer→janela, garantida em um único ponto (`destroyRendererAndWindow`).
3. **Dangling/use-after-free**: impossível por construção — o renderer nunca
   sobrevive à janela; recriação de surface = renderer NOVO.
4. **Double release**: impossível — ponteiro zerado no mesmo passo do
   release; `surfaceDestroyed` duplicado/tardio é no-op logado.
5. **`setBackend` com surface viva**: o runtime troca `window_` de mãos
   internamente SEM liberar a referência original (recria o renderer sobre
   a MESMA janela adquirida).

### Estados de surface (missão §IV) — independentes do lifecycle paused

`NO_SURFACE → AVAILABLE → (CHANGED_PENDING)* → DESTROYED → NO_SURFACE...`

- `surfaceCreated` com tamanho desconhecido (0×0 — o caso real do
  Android): janela aceita, renderer ADIADO para o primeiro
  `surfaceChanged` (que sempre precede qualquer render — spec Android);
- `surfaceChanged` com renderer adiado: cria o renderer com o tamanho
  real; com renderer vivo: `CHANGED_PENDING` aplicado no próximo frame
  (`Renderer::resize` recria swapchain/pbuffer);
- `renderFrame` em `NO_SURFACE`/`DESTROYED`/paused/renderer ausente:
  no-op contabilizado (missão §VIII) — NUNCA render/present;
- ordens inusuais (§VI): pause sem surface, resume sem surface,
  surfaceCreated com surface viva (recria), janela nula (ignora),
  surfaceDestroyed sem surface (no-op) — tudo tolerado.

### Surface nos backends (sem JNI — missão §II.4/§IX/§XII)

- **Vulkan**: `VK_USE_PLATFORM_ANDROID_KHR` + `vkCreateAndroidSurfaceKHR`
  sobre o `ANativeWindow*` (o backend só enxerga o `void*` opaco da
  `NativeWindowHandle`); `VK_KHR_android_surface` é VALIDADA contra as
  extensões reportadas — ausente = erro preciso, sem presunção (§X).
- **GLES/EGL**: `eglGetDisplay(EGL_DEFAULT_DISPLAY)` + config
  `EGL_WINDOW_BIT` + `eglCreateWindowSurface` (janela);
  versão 3.2→3.1→3.0 detectada (mín. 3.0 — §XIII); device-only no Android
  depende de surfaceless context (falha honesta se o driver não suportar).

## Consequências

- O teste crítico CREATE→RENDER→DESTROY→RECREATE→RENDER (§XXVIII) e
  RUNNING→PAUSE→RESUME→RUNNING (§XXIX) passam no Linux contra backends
  REAIS com os mesmos fontes do APK; o que permanece sem execução neste
  ambiente é apenas o par acquire/release do NDK (compilado no APK,
  exercitado em dispositivo — UNAVAILABLE aqui).
- A `eng::rhi` abstraction não mudou: kinds e handle opaco já existiam
  desde a FASE 4.
