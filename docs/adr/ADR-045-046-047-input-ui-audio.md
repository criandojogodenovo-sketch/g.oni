# ADR-045 — Input canônico com ações

- **Status:** aceito (FASE 9)
- **Contexto:** missão §6.1–§6.4 — o gameplay não pode conhecer
  dispositivos/códigos físicos; Android KeyCodes ficam fora da engine.
- **Decisões:**
  1. **Eventos canônicos:** a fronteira (TU JNI/Kotlin) CONVERTE
     MotionEvent/KeyCode para `eng::input::{InputEvent, Key, TouchPhase}`
     antes de cruzar; KeyCodes Android jamais chegam ao C++.
  2. **Estado puro:** `TouchState` por pointer ID (posição/delta/pressão/
     fase) com janela por update; `InputSystem` com fila → update →
     estado (pressed/released têm exatamente um frame de janela). Toques
     encerrados saem no fim do update em que ocorreram (visíveis durante
     ele para a semântica released).
  3. **Ações:** gameplay consulta `action("jump")` com fontes combinadas
     (tecla canônica, zona de toque em FRAÇÕES da tela, botão de gamepad
     declarado para coleta futura — §6.2). Configuração por asset JSON
     (`input.json`) via `ActionBindings::fromJson` — o editor da FASE 8
     gerencia o arquivo no Asset Browser (§D6).
  4. **Dispositivos:** Touch completo; Keyboard completo no modelo;
     Mouse/Gamepad são `DeviceKind` com o mesmo pipeline — coleta quando
     as plataformas a tiverem (§6.2 "quando disponíveis").
  5. **Separação editor×jogo (§6.4):** os gestos do EDITOR (FASE 8) seguem
     direto ao documento; o input do JOGO vive no runtime — no editor, só
     em PLAY (ferramenta PAN/MOVER como override explícito de debug).
- **Consequências:** gameplay portável entre dispositivos; semântica de
  janela testável em estado puro (9 casos/61 asserções sem plataforma).

# ADR-046 — UI em draw-list com fonte pontilhada

- **Status:** aceito (FASE 9)
- **Contexto:** §6.5–§6.7 — UI da engine (não Android Views); a abstraction
  RHI NÃO tem texturas (FASE 4).
- **Decisões:**
  1. **eng::ui não conhece RHI:** produz `std::vector<UiQuad>` (rect
     absoluto em DESIGN-RESOLUTION + cor + camada) que o HOST desenha
     com o pipeline pos+cor existente. A UI é testável sem GPU.
  2. **Fonte 5×7 pontilhada:** cada pixel aceso do glifo é um QUAD de cor
     (95 glifos ASCII; gerada por script — `scripts/gen_ui_font.py`).
     Texto legível em qualquer DPI (pixel escala). Limitação honesta com
     gatilho de upgrade: quando o RHI ganhar texturas, a fonte migra para
     atlas sem mudar a API (`textQuads` continua).
  3. **Image v1 = cor sólida:** layout/hit-test/hierarquia prontos; o
     CONTEÚDO aguarda texturas no RHI.
  4. **Layout:** rect relativo ao pai + anchors fracionários (default
     SEM stretch — bug do default fill-parent pego pelos testes);
     design-resolution → tela com fator de escala (resolução-independente
     §6.6). Hit-test top-most (depth-first reverso).
  5. **Eventos:** fn-ptr + contexto sem captura (padrão ADR-004) —
     Button(pressed/released/click), Slider(valueChanged por arraste).
- **Consequências:** UI completa de estrutura/interação já em uso; custo
  por caractere ~10-35 quads (irrelevante para HUDs; documentado).

# ADR-047 — Áudio pull com mixer por software

- **Status:** aceito (FASE 9)
- **Contexto:** §6.8–§6.10 — abstração de backend; lifetime sem vazamentos;
  API preparada para NI-Script (§9/§18).
- **Decisões:**
  1. **Mix por software no engine:** vozes (gain × bus) somadas em f32
     interleaved com clamp; o backend APENAS entrega os buffers ao
     dispositivo — o mix é idêntico em toda plataforma e testável
     matematicamente (13 casos/69-70 asserções, sem device).
  2. **Padrão pull:** o backend chama `mix(out, frames)` (callback de
     áudio). ÚNICA exceção documentada ao single-thread: um mutex protege
     a lista de vozes (janelas curtas; stress concorrente no suite e
     TSan-ready).
  3. **Backend AAudio via dlopen** (`libaaudio.so` + dlsym — mesmo padrão
     dos backends gráficos): sem link edit; API < 26 → erro preciso (o
     chamador decide). Null backend para testes/CI. O handle não é
     dlclose'd (padrão ADR-037).
  4. **Sound × Music:** Sound = buffer decodificado (SFX); Music =
     leitura única do arquivo com DECODE progressivo por janelas de 16k
     frames (streaming real §6.10). Handles geracionais: obsoleto é no-op
     seguro; vozes encerradas coletadas no `tick()` do jogo.
  5. **Lifecycle:** pauseAll/resumeAll/stopAll mapeiam onPause/onResume;
     loses de foco cancelam toques (upstream na Activity).
- **Consequências:** latência = callback do AAudio; streaming limita o
  trabalho por pull; dispositivo real só verificável em hardware
  (IMPLEMENTED+BUILT; DEVICE TESTED pendente — evidência por estágio).
