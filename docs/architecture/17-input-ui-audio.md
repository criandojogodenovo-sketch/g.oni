# Arquitetura — Input · UI · Audio (FASE 9)

> `eng::input`, `eng::ui`, `eng::audio` — sistemas de gameplay interativo
> (missão §6). Nenhum conhece Android/RHI (§6.1/§6.7/§6.9).

## eng::input (ADR-045)

```text
Android MotionEvent/KeyCode        (Kotlin: conversão CANÔNICA)
        ↓ InputEvent {TouchPhase|Key}   ← fila (UI thread)
eng::input::InputSystem.update()   ← 1× por frame (tick do runtime)
        ↓
TouchState (id/posição/delta/pressão)  ·  Ações "jump"/"attack"…
        ↓
Gameplay consulta AÇÕES/estado — nunca códigos de dispositivo
```

- Zonas de toque em **frações da tela** (resolução-independente);
- `input.json` (asset) → `ActionBindings::fromJson` (round-trip testado);
- Mouse/Gamepad: `DeviceKind` declarado no pipeline — coleta futura.

## eng::ui (ADR-046)

```text
UiDocument (árvore: Container/Panel/Button/Label/Image/Slider/ProgressBar)
        · layout: rect relativo + anchors fracionários (default sem stretch)
        · design-resolution → tela (+fator DPI)
        · hit-test top-most · eventos fn-ptr sem captura
        ↓ buildDrawList()
std::vector<UiQuad> {rect absoluto, cor, camada}   ← SEM RHI
        ↓ HOST desenha (pipeline pos+cor das FASES 5–7)
Texto: fonte 5×7 pontilhada (quads de cor — script-regenerável;
upgrade para atlas quando o RHI tiver texturas)
```

## eng::audio (ADR-047)

```text
Sound (WAV decodificado) ─┐
Music (arquivo, decode    ├─ Voice {volume, bus, loop, pause, cursor}
       progressivo 16k)  ─┘        ↓ gain = voz × bus
AudioMixer.mix(f32*, frames) ← THREAD DE ÁUDIO (pull; mutex único)
        ↑                            ↑
IAudioBackend (pull)          tick() (jogo): coleta vozes mortas
 ├── Null (testes/CI)
 └── AAudio (Android, dlopen libaaudio.so — API<26 → erro preciso)
WAV: RIFF PCM8/16/24/32f → f32 normalizado; chunks extras pulados
```

## Integração (§8 da missão FASES 8–10)

- **Runtime do jogo (FASE 7):** `AndroidRuntime` ganha `InputSystem` —
  `GoniActivity.onTouchEvent` converte para canônico e enfileira;
  1 update por frame; surfaceChanged informa o tamanho da tela.
- **Editor (FASE 8):** em PLAY os toques do viewport alimentam o input do
  RUNTIME (`gameTouch`); câmera do editor exige ferramenta PAN/MOVER
  (separação §6.4). Config por assets: `input.json` no Asset Browser.
- **Aguarda:** gameplay real consome ações (FASE 10 física + FASE 11
  NI-Script `input.jump`).

## Testes (Linux — estado puro, mesmos binários do APK)

- `input`: 12 casos/76 asserções — down/move/up, 3 dedos concorrentes,
  identidade por id, pressão, janela por update, teclado
  pressed/released, ações tecla+zona, JSON round-trip, reset + testes da
  remediação da auditoria final 4–10 (delta acumula por janela e zera,
  frameStamp carimba o update da fase, `pressed` de zona dispara UMA vez
  com dedo parado).
- `ui`: 8 casos/40 asserções — hierarquia, anchors/escala, hit-test
  top-most, click/cancel de botão, slider/progress, fonte, resoluções.
- `audio`: 14 casos/75 asserções — WAV PCM16/LIST/erros, play/stop/
  pause/resume/volume/bus/loop, mono→estéreo, lifecycle, streaming com
  fim e loop, stress concorrente (pull × play/stop), NullBackend + teste
  da remediação (mensagem de bits não suportados sem UB).
