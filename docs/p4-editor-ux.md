# P4.1 — Editor no device: bugs críticos + gizmos utilizáveis

Base: `203e5c9` (P3.5). Fase de CORREÇÃO + USABILIDADE — nenhuma arquitetura nova.
Primeira sessão real de UX no Realme C33 (Unisoc T612) produziu a lista D1–D8;
cada defeito virou código, teste e estado abaixo. Gate: **31/31 Linux debug+release**.

## Tabela de defeitos

| # | Defeito (repro device) | Causa raiz | Correção | Estado |
|---|---|---|---|---|
| D1 | 1ª seleção move; ao selecionar outra coisa as setas não aparecem e o drag não responde | **`selectEntity()` (hierarquia/menus) só atualizava a var Kotlin** — nunca chamava `nativeEditorSelect`; a seleção C++ (fonte do gizmo/hit-test/render) ficava na entidade velha. Além disso nenhum evento (tool/play/stop/tap) matava um drag em voo | Fonte única de verdade: `selectEntity` agora chama `nativeEditorSelect` (erro honesto via toast). Re-armo determinístico no documento: `select()`/`deselect()`/`setTool()`/`play()`/`stop()`/`viewportTap()` chamam `gizmoDragEnd()` — nenhum estado de drag sobrevive a mudança de contexto | **FIXED** |
| D2 | Gizmo de mover "calcula mal" a entrada na cena | **Bug de unidade do P1**: `screenDistanceTo` devolve px de TELA mas o raio de acerto era convertido px→mundo (`pxToWorld(kHitPx)`) — tolerância escalava com o zoom (~4px em zoom mínimo; ~256px em zoom máximo). Os testes antigos só passavam porque tocavam no centro EXATO dos handles | Hit-test compara px de tela contra px de tela (`hitPx(uiScale)`) — alvo constante no espaço do ecrã em qualquer zoom; novo teste de regressão com zoom 8 e 48 | **FIXED** |
| D3 | Gizmo de rotação minúsculo, sem seta, impossível de usar | Raio do anel derivado dos bounds sem clamp mínimo em px; sem affordance | Raio do anel = `max(borda dos bounds + 26dp, 64dp)` — agarrável em qualquer zoom; handle dot acoplado ao ângulo + spoke radial + chevron (ponta de seta); alvo de toque 48dp de diâmetro | **FIXED** |
| D4 | Sem gizmo utilizável de escala | Handles de canto existiam a 13px, sem alvo de toque ≥48dp, sem affordance | Métricas em dp × densidade do device (`Viewport::setUiScale`): handle visual 28–40px, alvo de toque 48dp; MOVE = 4 setas (±X/±Y) + quadrado central; SCALE = 4 cantos + **4 handles de aresta** (escala de UM eixo — `ScaleEdgeE/W/N/S`) | **FIXED** |
| D5 | Script criado+anexado, Play → nada acontece; sem feedback de erro | **Ni-Script não tinha operadores compostos** (`+=`, `-=`, `*=`, `/=`) — o script do autor (`position.x -= dt`) NÃO compilava; o erro ia só para o logcat (ENG_ERROR), invisível na UI | (a) Lexer/Parser: `+= -= *= /=` com desugar `x op= v → x = x op v` (reusa o caminho NEST_SET/DYN_SET do emitAssign); (b) `NiScriptStats` no runtime (found/compiled/failed/instances/ticks/faults/primeiro erro/entidade falha); (c) marcos `SCRIPT_COMPILE`/`SCRIPT_FAULT` no diagnóstico persistido; (d) UI: toast com o erro + HUD do Play ("Scripts: N inst · T ticks · F faults") ao vivo | **FIXED** |
| D6 | Áudio adicionado, Play → silêncio (AAudio builder null no C33 → NullBackend), sem aviso | Sem backend alternativo; estado do backend não chegava à UI | (a) **OpenSlEsBackend** (dlopen libOpenSLES.so — caminho Legacy AudioTrack; buffer queue i16 estéreo; AudioAdapt + CallbackGate, disciplina P3.4); (b) **AutoAudioBackend**: cadeia AAudio → OpenSL ES → erro composto, seleção logada (`AUDIO_BACKEND_SELECTED`); (c) `EditorHost::audioStatusLine()` + JNI + **HUD do Play** ("Áudio: running: opensl …" / "null: device sem AAudio/OpenSL …") + toast na entrada do Play | **FIXED** (som real se o HAL abrir; aviso honesto quando não abre — sem fallback silencioso) |
| D7 | Assets com opções mortas | Menu de áudio mostrava 4 itens com handler de 3 índices: "Ouvir (preview)" disparava RENOMEAR; "Apagar" era no-op (o `nativeEditorAudioPreview` existia e não era chamado) | `assetMenuDialog` reescrito com handlers EXPLÍCITOS por item (lista de lambdas, sem índice compartilhado): áudio = Ouvir/Renomear/Mover/Apagar; scripts = Abrir no editor/Renomear/Mover/Apagar; texturas = **Aplicar no sprite selecionado**/…; apagar com confirmação; erro honesto em todas as falhas | **FIXED** |
| D8 | Botão de configuração do projeto abre "criar novo" | O "☰" abria o menu com "Novo projeto…" primeiro; "Configurações" era só um renomear disfarçado | "Configurações do projeto…" é o PRIMEIRO item e abre sheet REAL: nome editável (salva via `setProjectName`), camadas da cena, timestep da física (1/60 fixo, acumulador) e **estado vivo do backend de áudio** | **FIXED** |

## Regras invioláveis — cumprimento

- **Zero UI morta**: D7/D8 rewired; nenhuma opção sem handler; erros sempre mostrados.
- **Zero gizmo fake**: todas as affordances são hit-testáveis (4 setas, arestas, anel).
- **Nenhum fallback silencioso**: cadeia de áudio loga a seleção; HUD mostra o estado; scripts expõem erro/entidade.
- **Device-verified**: o que NÃO foi testado em device está marcado abaixo.
- **Histórico preservado**: nenhum sistema existente removido; métricas do P1 ampliadas (dp), não trocadas.

## Áudio — comportamento em device

1. `createDefaultBackend()` (Android) devolve a cadeia Auto.
2. AAudio tenta abrir (mesmos marcos granulares do P3.4/P3.5).
3. Recusado (o caso do Unisoc: builder null) → OpenSL ES tenta (marcos `AUDIO_OSLE_*`).
4. Vencedor anunciado em `AUDIO_BACKEND_SELECTED`; HUD do editor mostra
   `running: <backend> <params>`; recusas compostas → NullBackend gracioso do host
   (P3.5) + HUD `null: device sem AAudio/OpenSL — som indisponível (nova tentativa no
   próximo resume)`.

Mixer/vozes intocados — a troca é só do backend (regra da missão).

## NI-Script — linguagem

- `x += v`, `x -= v`, `x *= v`, `x /= v` (globais, locais e campos de binding —
  `me.position.x -= delta()` incluído).
- Desugar no parser: `x op= v` → `x = x op v` (nó do alvo reusado como operando
  esquerdo; arena do parser — sem dupla posse). Sema/Compiler 1:1 o caminho de
  atribuição existente.

## Testes novos (regressão editor `[p41]`)

- D1: A→drag→B→drag funciona; troca de ferramenta/play/stop matam drag vivo.
- D2: raio de acerto constante em px (zoom 8 e 48).
- D3: anel ≥64px em zoom mínimo (com pan).
- D4: arestas E/N escalam um eixo; cantos priorizados quando tocados.
- D3/D4: `uiScale=2` amplia alvos (48dp); handle visual 28–40px.
- D5: `+= -= *= /=` em globais e campos (engine: `[ni][p41]`); CRLF tolerado.
- D5: stats visíveis (erro de compilação + entidade; ticks; faults; first update).

## Gates

- Linux debug: **31/31** (`ctest --preset linux-debug`) — inclui 127 casos do editor.
- Linux release: **31/31**.
- CI Android (APK): segue no workflow (`assembleDebug` arm64-v8a) — o binário device
  desta fase foi validado por compile+testes; o teste em DEVICE REAL (Realme C33)
  permanece o passo do utilizador conforme protocolo das fases anteriores.
- APK+SHA256: gerados pelo pipeline de release da mesma forma que o P3.5.

## Estado honesto / limitações

- Os testes de crash forense (P3.1/P3.5, fork+ASan) mostraram-se intermitentes em
  sandbox local SEM GPU/CI (presentes também na base `203e5c9` sem as mudanças
  desta fase) — no CI oficial são estáveis. Nada desta fase os toca.
- D6: o OpenSL ES abre o caminho Legacy do framework, mas o HAL do dispositivo é
  a última palavra; se AMBOS recusarem, o estado é mostrado (nunca calado).
- As camadas no sheet de configurações listam hoje o estado real do engine
  (GAME padrão — render+física); edição de camadas é trabalho do P4.2.
