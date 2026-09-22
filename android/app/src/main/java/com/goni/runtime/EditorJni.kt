package com.goni.runtime

import android.view.Surface

/**
 * Fronteira JNI do EDITOR (FASE 8) — espelho 1:1 de EditorJni.cpp.
 *
 * Regras (missão §2/§8): NENHUMA lógica de engine aqui — o Kotlin é a
 * camada Android (chrome touch); todo estado/comando vive no C++
 * (eng::editor). Entidades são Long empacotado (valor, não ponteiro);
 * listas vêm como snapshot TSV (uma linha por item, campos por \t).
 */
object EditorJni {

    init {
        // libgoni.so também embute o editor — mesma library do runtime.
        System.loadLibrary("goni")
    }

    // --- P3.1: diagnóstico de startup persistente + crash handler ---------
    // Chamado pela Activity ANTES de qualquer outra chamada nativa: abre
    // filesDir/goni_startup.log (cada estágio é gravado na HORA — sobrevive
    // à morte do processo) e instala o handler de crash (grava goni_crash.log
    // e RE-ENTREGA o sinal — tombstone/debuggerd preservados).
    fun bootstrap(context: android.content.Context) {
        nativeStartupInit(context.filesDir.absolutePath)
    }

    // --- host / surface / lifecycle ------------------------------------------

    external fun nativeEditorCreate(backend: String, workspaceRoot: String): Long
    external fun nativeEditorDestroy(handle: Long)
    external fun nativeEditorSurfaceCreated(handle: Long, surface: Surface)
    external fun nativeEditorSurfaceChanged(handle: Long, width: Int, height: Int)
    external fun nativeEditorSurfaceDestroyed(handle: Long)
    external fun nativeEditorOnPause(handle: Long)
    external fun nativeEditorOnResume(handle: Long)
    external fun nativeEditorRenderFrame(handle: Long, deltaSeconds: Float): Boolean
    external fun nativeEditorSetBackend(handle: Long, backend: String)

    // --- projeto (§8.1) -------------------------------------------------------

    external fun nativeEditorNewProject(handle: Long, name: String): Boolean
    external fun nativeEditorOpenProject(handle: Long, relPath: String): Boolean
    external fun nativeEditorSaveProject(handle: Long): Boolean
    external fun nativeEditorProjectName(handle: Long): String?
    /** P4.2 (B-A): NOME DA PASTA real do projeto no disco (export/cena). */
    external fun nativeEditorProjectFolder(handle: Long): String?
    /** P4.2 (B-A): zip do projeto (C++, entradas embrulhadas) → relPath. */
    external fun nativeEditorExportProjectZip(handle: Long, zipRelPath: String): Boolean
    /** P4.2 (B-A): extrai zip no workspace; devolve a pasta criada. */
    external fun nativeEditorImportProjectZip(handle: Long, zipRelPath: String, preferredName: String): String?
    external fun nativeEditorHasProject(handle: Long): Boolean
    external fun nativeEditorSetProjectName(handle: Long, name: String): Boolean

    // --- startup (P3 §0 — bug Android "AlreadyExists") -------------------------
    //
    // A POLÍTICA (criar quando não há NENHUM projeto / reabrir o último
    // usado / default / primeiro) vive no C++ e é testada no Linux; a
    // Activity chama UM ponto e só reporta o erro controlado.

    /** Cria OU reabre o projeto da política de startup; devolve o nome
     * do projeto aberto, ou null + lastError (jamais AlreadyExists no
     * caminho automático). */
    external fun nativeEditorEnsureProject(handle: Long): String?
    /** Projetos do workspace (linhas \n — dirs com project.goni.json). */
    external fun nativeEditorListProjects(handle: Long): String?
    /** Estado completo do host no logcat [GONI] (diagnóstico de crash). */
    external fun nativeEditorDumpState(handle: Long, origin: String)

    // --- P3.1: diagnóstico (FASES 4/5/6) --------------------------------------

    /** Inicializa o diagnóstico persistente (filesDir) + crash handler. */
    external fun nativeStartupInit(dir: String)
    /** Marca estágio de startup (persistido na hora + logcat [GONI]). */
    external fun nativeStartupMark(stage: String, status: String, detail: String?)
    /** true se há relatório de crash de execução anterior. */
    external fun nativeStartupHasCrashReport(): Boolean

    // --- P3.5 (T3): watchdog de hang da main thread ------------------------------

    /** Captura ESTA thread (main) como alvo do watchdog e instala os
     *  handlers de SIGUSR1/SIGUSR2. Chamar UMA vez no onCreate. */
    external fun nativeWatchdogArm()
    /** Prova de vida da main (chamado pela runnable postada pelo pinger). */
    external fun nativeWatchdogHeartbeat()
    /** Verifica expiração; dispara pthread_kill(main, SIGUSR1)+dump quando
     *  vencido. Retorna true quando disparou (pinger loga). */
    external fun nativeWatchdogEvaluate(): Boolean

    // --- cena (§8.2) ------------------------------------------------------------

    external fun nativeEditorNewScene(handle: Long): Boolean
    external fun nativeEditorSaveScene(handle: Long, relPath: String): Boolean
    external fun nativeEditorLoadScene(handle: Long, relPath: String): Boolean
    external fun nativeEditorSceneDirty(handle: Long): Boolean

    // --- entidades / hierarquia (§8.2/§8.3) --------------------------------------

    /** TSV: depth \t name \t packedId (linha por nó). */
    external fun nativeEditorHierarchy(handle: Long): String?
    external fun nativeEditorCreateEntity(handle: Long, name: String, parentPacked: Long): Long
    external fun nativeEditorDeleteEntity(handle: Long, packed: Long): Boolean
    external fun nativeEditorRenameEntity(handle: Long, packed: Long, name: String): Boolean
    external fun nativeEditorDuplicateEntity(handle: Long, packed: Long): Long
    external fun nativeEditorReparentEntity(handle: Long, packed: Long, parentPacked: Long): Boolean
    /** [px,py,pz, rx,ry,rz(graus), sx,sy,sz]. */
    external fun nativeEditorGetTransform(handle: Long, packed: Long): FloatArray?
    external fun nativeEditorSetTransform(
        handle: Long, packed: Long,
        px: Float, py: Float, pz: Float,
        rx: Float, ry: Float, rz: Float,
        sx: Float, sy: Float, sz: Float,
    ): Boolean
    external fun nativeEditorSelection(handle: Long): Long

    // --- seleção direta / ferramentas / gizmo (P1) ------------------------------------

    /** Seleciona a entidade no DOCUMENTO (borda no viewport, alvo do gizmo). */
    external fun nativeEditorSelect(handle: Long, packed: Long): Boolean
    /** Revisão do estado de seleção/transform — poll para live sync da UI. */
    external fun nativeEditorSelectionRevision(handle: Long): Long
    /** tool: 0=Select, 1=Move, 2=Rotate, 3=Scale. */
    external fun nativeEditorSetTool(handle: Long, tool: Int)
    external fun nativeEditorGetTool(handle: Long): Int
    /** P4.1 (T1/D3/D4): densidade do device (dp→px) — alvos de toque do
     * gizmo escalam por isto (≥48 dp). Instalar no startup + config change. */
    external fun nativeEditorSetUiScale(handle: Long, scale: Float)
    /** Handle do gizmo sob o toque (0=nenhum, 1=centro, 2=eixoX, 3=eixoY,
     * 4=rotação, 5-8=cantos, 9-12=arestas) — e INICIA o drag quando acerta. */
    external fun nativeEditorGizmoDragBegin(handle: Long, x: Float, y: Float): Int
    /** Arraste do gizmo até a posição ABSOLUTA do pointer (aplica ao ECS). */
    external fun nativeEditorGizmoDragTo(handle: Long, x: Float, y: Float): Boolean
    external fun nativeEditorGizmoDragEnd(handle: Long)
    /** ADD → Sprite: entidade com SpriteData default, selecionada. */
    external fun nativeEditorCreateSprite(handle: Long, name: String): Long

    // --- componentes / inspector (§8.4) --------------------------------------------

    /** TSV: typeName \t removable(0/1). */
    external fun nativeEditorComponentCatalog(handle: Long): String?
    /** TSV: typeName \t removable(0/1) — componentes PRESENTES na entidade. */
    external fun nativeEditorEntityComponents(handle: Long, packed: Long): String?
    /** TSV: fieldPath \t typeName \t value. */
    external fun nativeEditorComponentFields(handle: Long, packed: Long, component: String): String?
    external fun nativeEditorSetComponentField(
        handle: Long, packed: Long, component: String, fieldPath: String, value: String,
    ): Boolean
    external fun nativeEditorAddComponent(handle: Long, packed: Long, component: String): Boolean
    external fun nativeEditorRemoveComponent(handle: Long, packed: Long, component: String): Boolean

    // --- viewport (§8.6) / play-stop (§8.7) -------------------------------------------

    /** Entidade sob o toque (0 = nenhuma) — e a SELECIONA. */
    external fun nativeEditorViewportTap(handle: Long, x: Float, y: Float): Long
    external fun nativeEditorViewportPan(handle: Long, dx: Float, dy: Float)
    external fun nativeEditorViewportZoom(handle: Long, factor: Float, focusX: Float, focusY: Float)
    external fun nativeEditorMoveEntity(handle: Long, packed: Long, dx: Float, dy: Float): Boolean
    external fun nativeEditorPlay(handle: Long): Boolean
    external fun nativeEditorStop(handle: Long)
    external fun nativeEditorIsPlaying(handle: Long): Boolean
    /** P4.2 (T5 — Modo Jogo): PAUSE do runtime (tick congela; render vivo). */
    external fun nativeEditorSetPaused(handle: Long, paused: Boolean)
    external fun nativeEditorIsPaused(handle: Long): Boolean

    // --- assets (§8.5) ------------------------------------------------------------------

    external fun nativeEditorAssetCategories(handle: Long): String?
    /** TSV: name \t id \t registered \t sourcePath. */
    external fun nativeEditorAssetList(handle: Long, category: String): String?
    external fun nativeEditorAssetImport(handle: Long, tempRelPath: String, category: String, name: String): Boolean
    external fun nativeEditorAssetRename(handle: Long, category: String, name: String, newName: String): Boolean
    external fun nativeEditorAssetDelete(handle: Long, category: String, name: String): Boolean
    external fun nativeEditorAssetMove(handle: Long, fromCategory: String, name: String, toCategory: String): Boolean

    /** P4.1 (T2/D5): estatística do runtime de scripts — TSV
     * "found\tcompiled\tfailed\tinstances\tticks\tfaults\terro\tfault".
     * Fonte do toast/painel VISÍVEL no Play (o silêncio era o D5). */
    external fun nativeEditorScriptStats(handle: Long): String?
    /** P4.1 (T3/D6): estado do áudio para o HUD — "off" |
     * "running:<backend> <device>" | "null:<motivo>". */
    external fun nativeEditorAudioStatus(handle: Long): String?

    // --- imagens/texturas (evolução P0) -----------------------------------------------------

    /** "WxH rgba|rgb" quando o asset é imagem válida (decode REAL); null caso contrário. */
    external fun nativeEditorAssetImageInfo(handle: Long, category: String, name: String): String?
    /** Nomes dos assets de textura (linhas \n) — p/ picker de SpriteData. */
    external fun nativeEditorListTextures(handle: Long): String?

    // --- scripts NI-Script como assets (evolução P0-7, ADR-053) ----------------------------

    /** Nomes dos scripts do projeto (linhas \n); vazio quando sem projeto. */
    external fun nativeEditorScriptList(handle: Long): String?
    /** Conteúdo do script; null + lastError em falha. */
    external fun nativeEditorScriptRead(handle: Long, name: String): String?
    /** Escreve o conteúdo (multi-KB ok — sem limite de nome). */
    external fun nativeEditorScriptWrite(handle: Long, name: String, content: String): Boolean
    /** Cria script novo com template válido (força .nis). */
    external fun nativeEditorScriptCreate(handle: Long, name: String): Boolean
    /** Apaga script (arquivo + registry). */
    external fun nativeEditorScriptDelete(handle: Long, name: String): Boolean
    /** TSV: linha 1 = "1" compilou / "0" falhou; depois "line\tcol\tmessage". */
    external fun nativeEditorScriptCompile(handle: Long, source: String): String?
    /** Anexa o script à entidade (NiScriptComponent.source = conteúdo). */
    external fun nativeEditorScriptAssign(handle: Long, packed: Long, name: String): Boolean

    /** Toque do JOGO em Play (§6.4 — separado dos gestos do editor). phase: 0=Down,1=Move,2=Up,3=Cancel. */
    external fun nativeEditorGameTouch(handle: Long, phase: Int, pointerId: Int,
                                       x: Float, y: Float, pressure: Float)

    /** Tamanho do viewport do JOGO em Play (zonas de toque em fração da tela). */
    external fun nativeEditorSetGameViewportSize(handle: Long, width: Int, height: Int)

    // --- P2: componentes authoráveis / animação / áudio ------------------------------

    /** TSV: typeName \t dependencyHint — catálogo ADDÁVEL à entidade (§2/§14). */
    external fun nativeEditorAddableComponents(handle: Long, packed: Long): String?
    /** TSV: name \t clip \t duration \t frames \t keys \t loop. */
    external fun nativeEditorAnimationList(handle: Long): String?
    external fun nativeEditorAnimationRead(handle: Long, name: String): String?
    external fun nativeEditorAnimationWrite(handle: Long, name: String, json: String): Boolean
    /** Cria animação nova (flipbook template — força .anim.json). */
    external fun nativeEditorAnimationCreate(handle: Long, name: String): Boolean
    external fun nativeEditorAnimationDelete(handle: Long, name: String): Boolean
    /** Anexa o clip à entidade (cria Animator; SpriteData quando há frames). */
    external fun nativeEditorAnimationAssign(handle: Long, packed: Long, name: String): Boolean
    /** Acrescenta frame (textura real do projeto); devolve o tempo do frame ou -1. */
    external fun nativeEditorAnimationAddFrame(handle: Long, name: String, texture: String): Float
    external fun nativeEditorAnimationSetMeta(handle: Long, name: String, loop: Boolean, fps: Float): Boolean

    // --- P4.6 (Bloco 4): keys TRS — autoraria da timeline ---------------------
    // track ∈ {"position","rotation","scale"}; rotation em GRAUS.
    /** Grava key (mesmo tempo ε substitui; inserção ordenada). */
    external fun nativeEditorAnimationAddKey(handle: Long, name: String, track: String, time: Float, x: Float, y: Float, z: Float): Boolean
    /** TSV "index\ttime\tx\ty\tz" (ordenado por tempo). */
    external fun nativeEditorAnimationKeyList(handle: Long, name: String, track: String): String?
    /** Edita/move key por índice. */
    external fun nativeEditorAnimationKeySet(handle: Long, name: String, track: String, index: Int, time: Float, x: Float, y: Float, z: Float): Boolean
    external fun nativeEditorAnimationKeyDelete(handle: Long, name: String, track: String, index: Int): Boolean
    /** PREVIEW da animação na entidade (Edit) — avança com o render frame. */
    external fun nativeEditorPreviewStart(handle: Long, packed: Long, clip: String): Boolean
    external fun nativeEditorPreviewStop(handle: Long)
    external fun nativeEditorPreviewing(handle: Long): Boolean
    /** Toca/para um asset WAV (P4.3/N1 — toggle: 2º toque no mesmo = stop). */
    external fun nativeEditorAudioPreview(handle: Long, name: String): Boolean
    /** P4.3 (N1): para o preview de áudio (idempotente). */
    external fun nativeEditorAudioPreviewStop(handle: Long)
    /** P4.3 (N1): há voice de preview viva? (fonte de verdade do botão). */
    external fun nativeEditorAudioPreviewPlaying(handle: Long): Boolean

    // --- P4.3 (Bloco 2): Ticks/Camadas — ADR-051 autorável ---------------------
    /** Camadas em TSV: name\ttimescale\tupdate\tphysics\trender. */
    external fun nativeEditorLayerList(handle: Long): String?
    external fun nativeEditorLayerAdd(handle: Long, name: String): Boolean
    external fun nativeEditorLayerSetTimeScale(handle: Long, name: String, ts: Float): Boolean
    external fun nativeEditorLayerSetParticipation(handle: Long, name: String, update: Boolean, physics: Boolean, render: Boolean): Boolean
    /** Timestep fixo da física em segundos (config da cena). */
    external fun nativeEditorPhysicsDt(handle: Long): Float
    external fun nativeEditorPhysicsSetDt(handle: Long, dt: Float): Boolean

    // --- P4.6 (Bloco 1): camadas de COLISÃO nomeadas (project settings) --------
    // ≠ nativeEditorLayerList (camadas de CENA/tick, ADR-051): estes
    // bitfields filtram pares de colisão e moram no project.goni.json.
    /** TSV: name\tbit (uma linha por camada nomeada, ordem da tabela). */
    external fun nativeEditorCollisionLayerList(handle: Long): String?
    /** Renomeia a camada do bit (erro = false + lastError). */
    external fun nativeEditorSetCollisionLayerName(handle: Long, bit: Long, name: String): Boolean
    /** Nova camada com o menor bit livre; devolve o bit (0 = erro). */
    external fun nativeEditorAddCollisionLayer(handle: Long, name: String): Long
    /** Nomes dos assets de áudio (linhas \n) — picker do Inspector (kind audio). */
    external fun nativeEditorListAudio(handle: Long): String?

    // --- P3 §3: materiais (assets/materials/<nome>.mat.json) ---------------------

    /** TSV: name \t shader \t tintR \t tintG \t tintB \t tintA. */
    external fun nativeEditorMaterialList(handle: Long): String?
    /** Conteúdo cru do material (JSON). */
    external fun nativeEditorMaterialRead(handle: Long, name: String): String?
    external fun nativeEditorMaterialWrite(handle: Long, name: String, json: String): Boolean
    /** Cria material novo (template lit com tint neutro — força .mat.json). */
    external fun nativeEditorMaterialCreate(handle: Long, name: String): Boolean
    external fun nativeEditorMaterialDelete(handle: Long, name: String): Boolean
    /** Nomes dos materiais (linhas \n) — picker do Inspector (kind material). */
    external fun nativeEditorListMaterials(handle: Long): String?

    // --- erro da última operação (toasts/diálogos) ----------------------------------------

    external fun nativeEditorLastError(handle: Long): String?

    // --- P4.5: snap do gizmo / fit do viewport ---------------------------------------

    /** Chips de snap da tool sheet: translação na grade / rotação 15°. */
    external fun nativeEditorSetSnap(handle: Long, translate: Boolean, rotate: Boolean)
    external fun nativeEditorGetSnapTranslate(handle: Long): Boolean
    external fun nativeEditorGetSnapRotate(handle: Long): Boolean
    /** Enquadra a seleção (ou a cena inteira) — cluster de zoom. */
    external fun nativeEditorViewportFit(handle: Long): Boolean

    // --- P4.5: undo/redo (command pattern por snapshots de cena) ----------------------

    external fun nativeEditorCanUndo(handle: Long): Boolean
    external fun nativeEditorCanRedo(handle: Long): Boolean
    external fun nativeEditorUndo(handle: Long): Boolean
    external fun nativeEditorRedo(handle: Long): Boolean
}
