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
    external fun nativeEditorHasProject(handle: Long): Boolean
    external fun nativeEditorSetProjectName(handle: Long, name: String): Boolean

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

    // --- assets (§8.5) ------------------------------------------------------------------

    external fun nativeEditorAssetCategories(handle: Long): String?
    /** TSV: name \t id \t registered \t sourcePath. */
    external fun nativeEditorAssetList(handle: Long, category: String): String?
    external fun nativeEditorAssetImport(handle: Long, tempRelPath: String, category: String, name: String): Boolean
    external fun nativeEditorAssetRename(handle: Long, category: String, name: String, newName: String): Boolean
    external fun nativeEditorAssetDelete(handle: Long, category: String, name: String): Boolean
    external fun nativeEditorAssetMove(handle: Long, fromCategory: String, name: String, toCategory: String): Boolean

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

    // --- erro da última operação (toasts/diálogos) ----------------------------------------

    external fun nativeEditorLastError(handle: Long): String?
}
