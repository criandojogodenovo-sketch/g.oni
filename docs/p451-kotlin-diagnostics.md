# P4.5.1 — Rede de diagnóstico Kotlin (handler de exceções + micro-marks)

> Contexto: o APK P4.5 "Curved Dark" (`0b5bcde`) estreou no Realme C33 com
> **5 mortes de startup silenciosas**. O handler existente (P3.1) apanha
> apenas **sinais nativos** — exceções Kotlin morriam SEM nenhuma evidência
> (sem `goni_crash.log`, sem tombstone nomeando a linha). P4.5.1 fecha a
> lacuna SEM tocar em comportamento (contrato §3 do P4.5 intacto).
>
> Verificação: **VERIFIED (código)** = inspeção/compilação local confirma.
> **VERIFIED (CI)** = CI Linux (ASan+UBSan, Werror) + CI Android verdes no
> commit. **VERIFIED (APK)** = auditado o artefato real do CI (o mesmo
> binário instalado no device). **NOT VERIFIED (device)** = exige a próxima
> execução no C33.

## 0. A evidência que motivou (forense do device)

| Sessões | Último mark | Janela da morte (mapeada no código) |
|---|---|---|
| 4 (pid 24087…24618) | `STARTUP_EDITOR_DOCUMENT ok` | DENTRO de `buildUi()` — build da UI P4.5 (OniUi/sheets/chips/1º frame): **zero marks** entre `EDITOR_DOCUMENT` e `STARTUP_EDITOR_UI` |
| 1 (pid 24646) | `STARTUP_ACTIVITY ok` | Entre `STARTUP_ACTIVITY ok` e `EDITOR_HOST begin`: `maybeOfferCrashExport()` → **`OniDialog.custom()` (código P4.5, roda SÓ quando há crash report anterior)** + `mkdirs` + entrada JNI (`registerBackendFactories` antes do begin) |

O nativo é inocente em todas: `libgoni.so` carrega, JNI ok, filesystem ok,
documento ok (`STARTUP_EDITOR_DOCUMENT ok` emitido pelo C++). A morte vive
na camada Kotlin reescrita pelo P4.5 — e era invisível porque o crash
handler atual só apanha sinais, não exceções.

## 1. KotlinCrashGuard (o handler que faltava)

**Arquivo novo:** `android/app/src/main/java/com/goni/runtime/KotlinCrashGuard.kt`

| Propriedade | Decisão |
|---|---|
| Instalação | `Thread.setDefaultUncaughtExceptionHandler` no onCreate, ANTES de qualquer código que possa lançar (logo após `DiagnosticsMirror.init`) |
| Idempotência | UMA vez por processo — Activity recriada NÃO re-encadeia (cadeia guard→prev preservada) |
| Evidência | `filesDir/goni_crash.log` (append) com a MESMA assinatura `[crash]` do handler nativo → o pipeline existente trata crash Kotlin igual ao nativo: `hasPreviousCrashReport` → espelho automático para `Download/GONI` na execução seguinte + crash-prompt |
| Formato | Linha 1: `[crash] kotlin \| phase=<FASE> \| thread=<nome> \| time=<epoch> \| <classe> \| <msg>` — segue o stack completo (a LINHA que o C33 nunca conseguiu mostrar) |
| Fase corrente | `@Volatile` atualizada pelos micro-marks R3 — o relatório nomeia a fase; o stack nomeia a linha |
| Espelho público | Best-effort BORNED: `DiagnosticsMirror.exportCrashLogIfPresent()` (fila assíncrona, prazo 2 s) — a cópia privada em filesDir já garante a evidência na execução seguinte de qualquer forma |
| Delegação | SEMPRE ao handler anterior — o comportamento de morte do sistema (diálogo do Android, ART, tombstone) é INTACTO; só ADICIONA forense antes |
| I/O | Java puro, válido aqui: exceção Kotlin corre no frame normal da JVM — NÃO é contexto de sinal (o handler nativo mantém suas regras async-signal-safe para sinais) |

**Escopo deliberado:** instalado apenas no `EditorActivity` (as 5 mortes são
todas do caminho do editor). O runtime-demo `GoniActivity` fica intocado
(contrato zero-mudança). LIMITATION: um crash futuro do runtime-demo
continua sem forense Kotlin — uma linha (`KotlinCrashGuard.install(this)`)
fecha isso quando houver evidência de necessidade.

## 2. R1 — `STARTUP_EDITOR_HOST` fecha com "ok"

`editor/src/EditorHost.cpp` (EditorHost::create): o begin era o ÚNICO mark
sem par "ok" — a janela entre o begin e o próximo mark do Kotlin nunca
fechava ("morreu a criar o host" vs "morreu depois do host" eram
indistinguíveis). Agora, após host + documento prontos:

```
STARTUP_EDITOR_HOST begin → STARTUP_FILESYSTEM ok
→ STARTUP_EDITOR_DOCUMENT ok → STARTUP_EDITOR_HOST ok ("host + documento prontos")
```

**Teste nativo novo** (`editor/tests/EditorTests.cpp`): "editor: P4.5.1 R1 —
STARTUP_EDITOR_HOST fecha com ok (par begin/ok)" — cria host em workspace
temporário, valida no DELTA do log: begin+ok presentes, ok DEPOIS do begin,
e ordem interna grep-ável (filesystem → documento → host fechado).

## 3. R2 — Auditoria do APK real (o binário que está no C33)

Artefato `goni-debug-apk` do CI Android @ `0b5bcde` (execução #56):

| Verificação | Resultado |
|---|---|
| SHA256 | `659166917a13be1e91763302f513042707515fa90f370c170d28f458e3650253` |
| Conteúdo | 42 entradas: 3 dex, `lib/{arm64-v8a,x86_64}/{libgoni.so,libc++_shared.so}`, res splash/launcher completos |
| `androidx` nos 3 dex (strings) | **ZERO ocorrências** — a dependency é intencionalmente vazia (build.gradle) |
| `androidx.core.splashscreen` nos dex | **ZERO** — a teoria `NoClassDefFoundError` de splashscreen está **REFUTADA**: as classes não existem no APK E nenhuma linha de código as referencia (se referenciassem, seria erro de COMPILO, não de runtime) |
| Splash | `windowBackground` = `@drawable/oni_splash` puro (layer-list: `@color/oni_bg` + logo + wordmark; 4 densidades de PNG presentes no APK) |
| Tema (API level do C33, Android 12) | `GoniTheme` usa só attrs `android:` de API 21+ (windowBackground/colorEdgeEffect/statusBarColor/navigationBarColor) — válidos para minSdk 24 |
| Android 12+ splash do sistema | Usa o ícone do LAUNCHER: `mipmap-anydpi-v26/ic_launcher.xml` → `@drawable/oni_launcher_mark.xml` (vector válido, refs `@color/oni_bg`/`#8AB4F8` presentes) — cadeia íntegra |

Consequência: a janela da morte #2 reduz-se a (a) `OniDialog.custom` do
crash-prompt, (b) `mkdirs` trivial, (c) entrada JNI antes do begin. Os
micro-marks R3 + o guard nomeiam exatamente qual.

## 4. R3 — Micro-marks que cobrem as duas janelas

Cada mark: persistido NA HORA (filesDir + espelho assíncrono) + logcat
`[GONI]`, e atualiza a fase do guard. Auditorias de tema/splash são
best-effort: falha = mark `failed` com a exceção no detalhe — NUNCA aborta
o startup (quem mataria de verdade é o framework; aí o guard pega).

| Mark | Onde | O que fecha |
|---|---|---|
| `UI_THEME` | onCreate, após `STARTUP_ACTIVITY ok` | Resolve o `windowBackground` do tema ativo (confirma = `oni_splash`) + ícone do launcher (usado pelo splash do sistema 12+) |
| `UI_SPLASH` | onCreate, após `UI_THEME` | Infla a cadeia completa do splash (layer-list + cores + PNGs por densidade) |
| — | onCreate, após `UI_SPLASH` | `maybeOfferCrashExport()` (OniDialog P4.5) roda DELIMITADO: morte nele = último mark `UI_SPLASH` + stack nomeando a linha |
| `UI_BUILD_START` | onCreate, antes de `buildUi()` | Abre a janela da morte #1 |
| `ONIUI_INIT` | buildUi, após header completo | Header card + chips + play (primeiro trecho P4.5: tokens/helpers Oni) |
| `UI_SHEETS` | buildUi, após `buildAssetsPanel()` | Sheets + scrim + adapters (a metade pesada do build) |
| `UI_FIRST_FRAME` | buildUi, após `setContentView` + insets + estados | UI anexada à janela, traversal agendado (o 1º frame em si já tem `FIRST_TRAVERSAL` no doFrame) |
| `STARTUP_EDITOR_HOST ok` (R1) | C++ (EditorHost::create) | Fecha a fase de criação do host |

Fases do guard (mais finas que marks, sem custo): `BOOTSTRAP → UI_THEME →
UI_SPLASH → CRASH_PROMPT → EDITOR_CREATE → UI_BUILD (ONIUI_INIT/UI_SHEETS/
UI_FIRST_FRAME) → STARTUP_EDITOR_UI → POST_PROJECT → UI_SYNC → UI_SCALE →
IDLE → RESUME`.

Sequência completa de startup após P4.5.1 (marks em ordem):

```
STARTUP_NATIVE_LIBRARY → STARTUP_JNI → STARTUP_APPLICATION → STARTUP_ACTIVITY
→ UI_THEME → UI_SPLASH → (crash-prompt opcional)
→ STARTUP_EDITOR_HOST begin → STARTUP_FILESYSTEM ok → STARTUP_EDITOR_DOCUMENT ok
→ STARTUP_EDITOR_HOST ok (R1 — NOVO)
→ UI_BUILD_START → ONIUI_INIT → UI_SHEETS → UI_FIRST_FRAME (NOVOS R3)
→ STARTUP_EDITOR_UI → STARTUP_POST_PROJECT → STARTUP_UI_SYNC
→ MIRROR_ENQUEUE → RESUME_RETURN → FIRST_TRAVERSAL → …
```

## 5. Como ler a PRÓXIMA morte (protocolo)

1. **`goni_crash.log` em `Download/GONI/`** (agora também para exceções
   Kotlin): linha `[crash] kotlin | phase=X` nomeia a FASE; o stack anexo
   nomeia a CLASSE/ARQUIVO/LINHA.
2. **`goni_startup.log`**: o último mark `UI_*`/`STARTUP_*` contextualiza a
   fase — sem depender de ADB (o C33 não tem).
3. Se a linha for `[crash] <sinal nativo>` (ex.: SIGSEGV), é o handler P3.1
   de sempre (formato [pc]/[fp]/maps — ver p33-symbolization.md).
4. Execução seguinte: o espelho automático (P3.2) publica o crash ANTES de
   qualquer carga; o crash-prompt oferece o zip completo.

## 6. Verificação

| Item | Estado | Evidência |
|---|---|---|
| R1: par begin/ok do EDITOR_HOST | VERIFIED (código + teste) | Teste nativo novo verde local (debug ASan e release); ordem filesystem→documento→host ok validada |
| R3: 6 micro-marks + fases | VERIFIED (código) | `EditorActivity.kt` — marks persistidos na hora via `nativeStartupMark` (mesma via P3.1) |
| Guard: forense Kotlin em `goni_crash.log` | VERIFIED (código) | Assinatura `[crash]` integra com `hasPreviousCrashReport`/espelho/crash-prompt existentes |
| Guard: não re-encadeia em Activity recriada | VERIFIED (código) | Flag `installed` idempotente |
| R2: splashscreen NoClassDef refutado | VERIFIED (APK) | 0 ocorrências de `androidx`/`splashscreen` nos 3 dex do artefato @0b5bcde (SHA256 no §3) |
| R2: tema/splash válidos p/ API do C33 | VERIFIED (código) | Atributos API 21+; cadeia de recursos completa no APK (§3) |
| Suíte de editor existente não regrediu | VERIFIED (local) | 167 casos: 155 passaram, 12 SKIP (sem GPU no container); únicos FAILs = 2 testes de sinal que falham idênticos no HEAD limpo neste container (kernel/ASan) e são verdes no CI |
| CI Linux (ASan+UBSan+Werror) + CI Android | VERIFIED (CI) | Verdes no commit P4.5.1 |
| APK P4.5.1 + SHA256 | VERIFIED (CI) | Artefato da execução Android deste commit |
| Morte real nomeada no device | NOT VERIFIED (device) | Exige a próxima morte (esperada: nenhuma, OU agora com forense completo) |
| Runtime-demo (GoniActivity) com guard | LIMITATION | Fora do escopo P4.5.1 (5 mortes são todas do editor); uma linha fecha quando necessário |
| Comportamento em caminho saudável | PRESERVADO | Zero mudanças em lógica de UI/JNI/gameplay; marks são writes log-only; auditorias de tema são read-only |
