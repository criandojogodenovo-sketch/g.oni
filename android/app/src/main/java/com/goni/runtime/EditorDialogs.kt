package com.goni.runtime

import android.content.Intent
import android.graphics.Typeface
import android.text.InputType
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * P4.5 — MENUS/PICKERS/DIÁLOGOS do editor (extensões do EditorActivity).
 * ZERO diálogos default: tudo via OniDialog (card curvo 24dp,
 * confirm/cancel nos cantos inferiores, fade+scale 120 ms). Cada item tem
 * handler EXPLÍCITO (auditoria D7 — nada de índice compartilhado).
 */

// --- menus da barra superior ----------------------------------------------------

internal fun EditorActivity.showProjectMenu() {
    val act = this
    val items = listOf(
        "Configurações do projeto…",
        "Novo projeto…", "Abrir projeto…", "Salvar projeto",
        "Pasta de exportação (SAF)…", "Exportar projeto (zip)…",
        "Importar projeto (zip)…", "Diagnóstico (logcat)",
        "Exportar diagnóstico (arquivos)…"
    )
    OniDialog.list(this, "Projeto", items) { which ->
        when (which) {
            // P4.1 (T4/D8): PRIMEIRO item e sheet REAL (nome, camadas,
            // timestep de física, estado do backend de áudio).
            0 -> showProjectSettingsSheet()
            1 -> inputDialog("Nome do novo projeto", "NovoJogo") { name ->
                if (EditorJni.nativeEditorNewProject(handle, name)) {
                    EditorJni.nativeEditorNewScene(handle)
                    refreshAll()
                } else toastErr(lastErrorText())
            }
            2 -> openProjectDialog()
            3 -> if (EditorJni.nativeEditorSaveProject(handle)) {
                // P4.2 (B-A): "Salvar projeto" persiste PROJETO + CENA.
                toastOk("Projeto + cena salvos")
            } else {
                toastErr(lastErrorText())
            }
            // P2 §17 — SAF: pasta de exportação com permissão PERSISTENTE.
            4 -> pickSafFolder()
            5 -> exportProjectZip()
            6 -> importProjectZip()
            // P3 §0 — diagnóstico: estado completo no logcat [GONI].
            7 -> {
                EditorJni.nativeEditorDumpState(handle, "menu-projeto")
                toast("Estado gravado no logcat (tag GONI)")
            }
            // P3.1 (FASE 6): exporta goni_startup.log + goni_crash.log.
            8 -> exportDiagnosticsZip()
        }
    }
}

internal fun EditorActivity.showSceneMenu() {
    val act = this
    val items = listOf("Nova cena", "Salvar cena…", "Carregar cena…")
    OniDialog.list(this, "Cena", items) { which ->
        when (which) {
            0 -> EditorJni.nativeEditorNewScene(handle).also {
                selection = 0L; refreshPanel()
            }
            1 -> inputDialog("Salvar cena em (relativo)", "main.json") { path ->
                if (!EditorJni.nativeEditorSaveScene(handle, path)) {
                    toastErr(lastErrorText())
                }
            }
            2 -> loadSceneDialog()
        }
    }
}

internal fun EditorActivity.showBackendMenu(button: TextView) {
    val act = this
    val options = listOf("auto", "vulkan", "gles")
    OniDialog.list(this, "Backend de render", options) { which ->
        EditorJni.nativeEditorSetBackend(handle, options[which])
        button.text = options[which]
    }
}

// --- startup / crash --------------------------------------------------------------

/** P3.1 (FASE 6): crash anterior → oferece o export ANTES de qualquer carga. */
internal fun EditorActivity.maybeOfferCrashExport() {
    val act = this
    if (!EditorJni.nativeStartupHasCrashReport()) return
    OniDialog.custom(
        this, "Crash anterior detectado",
        TextView(this).apply {
            text = "A execução anterior terminou em crash nativo.\n" +
                "Exportar o diagnóstico (startup + crash) agora?"
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            setTextColor(Oni.TEXT)
        },
        listOf(
            OniDialog.Btn("Agora não", accent = false),
            OniDialog.Btn("Exportar") { exportDiagnosticsZip() }
        )
    )
}

// --- SAF (P2 §17): zip do projeto ida e volta ------------------------------------

internal fun EditorActivity.pickSafFolder() {
    val act = this
    startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), 4101)
}

internal fun EditorActivity.exportDiagnosticsZip() {
    val act = this
    val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
        addCategory(Intent.CATEGORY_OPENABLE)
        type = "application/zip"
        putExtra(Intent.EXTRA_TITLE, "goni-diagnostics.zip")
    }
    startActivityForResult(intent, 4104)
}

internal fun EditorActivity.writeDiagnosticsZipTo(uri: android.net.Uri) {
    val act = this
    try {
        contentResolver.openOutputStream(uri)?.use { out ->
            ZipOutputStream(out).use { zip ->
                for (name in listOf("goni_startup.log", "goni_crash.log")) {
                    val f = File(filesDir, name)
                    if (!f.exists()) continue
                    zip.putNextEntry(ZipEntry(name))
                    f.inputStream().use { it.copyTo(zip) }
                    zip.closeEntry()
                }
            }
        }
        toastOk("Diagnóstico exportado")
    } catch (e: Exception) {
        toastErr("Falha ao exportar: ${e.message}")
    }
}

internal fun EditorActivity.exportProjectZip() {
    val act = this
    val project = EditorJni.nativeEditorProjectName(handle)
    if (project.isNullOrEmpty()) {
        toastErr("Nenhum projeto aberto")
        return
    }
    val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
        addCategory(Intent.CATEGORY_OPENABLE)
        type = "application/zip"
        putExtra(Intent.EXTRA_TITLE, "$project.goni.zip")
    }
    startActivityForResult(intent, 4102)
}

/** Zip REAL do projeto (P4.2/B-A): o C++ escreve .goni_export.zip no
 *  workspace; o Kotlin só copia para o SAF. */
internal fun EditorActivity.writeProjectZipTo(uri: android.net.Uri) {
    val act = this
    val project = EditorJni.nativeEditorProjectName(handle) ?: return
    val exportFile = File(File(filesDir, "projects"), ".goni_export.zip")
    try {
        if (!EditorJni.nativeEditorExportProjectZip(handle, ".goni_export.zip")) {
            toastErr(lastErrorText())
            return
        }
        contentResolver.openOutputStream(uri)?.use { out ->
            exportFile.inputStream().use { it.copyTo(out) }
        }
        toastOk("Projeto '$project' exportado")
    } catch (e: Exception) {
        toastErr("Export falhou: ${e.message}")
    } finally {
        exportFile.delete()
    }
}

internal fun EditorActivity.importProjectZip() {
    val act = this
    val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
        addCategory(Intent.CATEGORY_OPENABLE)
        type = "application/zip"
        putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/zip", "application/octet-stream"))
    }
    startActivityForResult(intent, 4103)
}

/** Importa o zip PARA O WORKSPACE (privado) e ABRE o projeto (P4.2/B-A):
 *  SEM newScene — o openProject restaura a última cena via marker. */
internal fun EditorActivity.importProjectZipFrom(uri: android.net.Uri) {
    val act = this
    val importFile = File(File(filesDir, "projects"), ".goni_import.zip")
    try {
        contentResolver.openInputStream(uri)?.use { input ->
            importFile.outputStream().use { output -> input.copyTo(output) }
        } ?: run {
            toastErr("Não foi possível ler o arquivo")
            return
        }
        val suggested = (uri.lastPathSegment?.substringAfterLast('/')
            ?: "").removeSuffix(".zip").ifEmpty { "Importado" }
        val folder = EditorJni.nativeEditorImportProjectZip(
            handle, ".goni_import.zip", suggested
        )
        if (folder == null) {
            toastErr(lastErrorText())
            return
        }
        if (EditorJni.nativeEditorOpenProject(handle, folder)) {
            refreshAll()
            toastOk("Projeto '$folder' importado e aberto")
        } else {
            toastErr(lastErrorText())
        }
    } catch (e: Exception) {
        toastErr("Import falhou: ${e.message}")
    } finally {
        importFile.delete()
    }
}

// --- project switcher (§2 — sheet curva com thumbnails + nome + data) -------------

internal fun EditorActivity.openProjectDialog() {
    val act = this
    val workspace = File(filesDir, "projects")
    // Diretórios ocultos (ex.: .import_tmp — staging do SAF) não são
    // projetos: o seletor lista apenas pastas reais de projeto.
    val projects = workspace.listFiles()
        ?.filter { it.isDirectory && !it.name.startsWith(".") } ?: emptyList()
    if (projects.isEmpty()) {
        toastErr("Nenhum projeto em ${workspace.name}")
        return
    }
    val column = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
    }
    val scroll = ScrollView(this).apply { addView(column) }
    var dlg: android.app.Dialog? = null
    for ((index, folder) in projects.withIndex()) {
        val name = folder.name
        val row = Oni.listRow(this, twoLine = true)
        // Thumbnail: primeira textura do projeto (ou placeholder glyph).
        val texturesDir = File(File(folder, "assets"), "textures")
        val firstImage = texturesDir.listFiles()
            ?.firstOrNull { it.isFile }?.name
        if (firstImage != null) {
            val bmp = thumbnailOfIn(name, "textures", firstImage)
            if (bmp != null) {
                row.addView(ImageView(this).apply {
                    setImageBitmap(bmp)
                    scaleType = ImageView.ScaleType.CENTER_CROP
                    background = Oni.rounded(act, Oni.RAISED, Oni.R_THUMB)
                    clipToOutline = true
                }, LinearLayout.LayoutParams(dp(48), dp(48)))
                row.addView(View(this), LinearLayout.LayoutParams(dp(12), dp(1)))
            }
        }
        val texts = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }
        texts.addView(TextView(this).apply {
            text = name
            setTextColor(Oni.TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            typeface = Typeface.DEFAULT_BOLD
        })
        texts.addView(TextView(this).apply {
            // Data (mono 11sp dim — §1.5): última modificação da pasta.
            val fmt = java.text.SimpleDateFormat("dd/MM/yy HH:mm",
                java.util.Locale.getDefault())
            text = fmt.format(java.util.Date(folder.lastModified()))
            setTextColor(Oni.TEXT_DIM)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            typeface = Typeface.MONOSPACE
        })
        row.addView(texts, LinearLayout.LayoutParams(
            0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        row.setOnClickListener {
            dlg?.dismiss()
            if (EditorJni.nativeEditorOpenProject(handle, name)) {
                // P4.2 (B-A): SEM newScene aqui — openProject restaura a
                // ÚLTIMA CENA do projeto (.goni_last_scene).
                refreshAll()
            } else {
                toastErr(lastErrorText())
            }
        }
        column.addView(row, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT))
        if (index < projects.size - 1) {
            column.addView(View(this), LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(6)))
        }
    }
    dlg = OniDialog.custom(this, "Abrir projeto", scroll,
        listOf(OniDialog.Btn("Cancelar", accent = false)))
}

// --- cenas ------------------------------------------------------------------------

internal fun EditorActivity.loadSceneDialog() {
    val act = this
    // Cenas vivem em <workspace>/<PASTA-do-projeto>/scenes (§8.1 —
    // scenesRoot do projeto). P4.2 (B-A): o nome da PASTA vem do documento.
    val project = EditorJni.nativeEditorProjectFolder(handle)
    if (project.isNullOrEmpty()) {
        toastErr("Nenhum projeto aberto")
        return
    }
    val scenesRoot = File(File(File(filesDir, "projects"), project), "scenes")
    val files = scenesRoot.listFiles()?.filter { it.isFile } ?: emptyList()
    if (files.isEmpty()) {
        toastErr("Nenhuma cena salva em ${project}/scenes")
        return
    }
    val names = files.map { it.name }
    OniDialog.list(this, "Carregar cena", names) { which ->
        if (EditorJni.nativeEditorLoadScene(handle, names[which])) {
            selection = 0L
            refreshPanel()
        } else {
            toastErr(lastErrorText())
        }
    }
}

// --- entidades ---------------------------------------------------------------------

internal fun EditorActivity.createEntityDialog() {
    val act = this
    inputDialog("Nome da entidade", "Entity") { name ->
        val parent = 0L // raiz (reparent pelo menu de contexto)
        val packed = EditorJni.nativeEditorCreateEntity(handle, name, parent)
        if (packed == 0L) {
            toastErr(lastErrorText())
        } else {
            selectEntity(packed)
        }
    }
}

/** ADD → Sprite (P1.10): um toque = entidade com SpriteData default,
 *  selecionada. Sem textura → placeholder xadrez no viewport. */
internal fun EditorActivity.addSpriteDialog() {
    val act = this
    inputDialog("Nome do sprite", "Sprite") { name ->
        val packed = EditorJni.nativeEditorCreateSprite(handle, name)
        if (packed == 0L) {
            toastErr(lastErrorText())
        } else {
            selectEntity(packed)
            toastOk("Sprite criado — importe uma imagem e escolha a textura no Inspector")
        }
    }
}

internal fun EditorActivity.entityMenuDialog(packed: Long) {
    val act = this
    val items = listOf(
        "Renomear…", "Duplicar", "Apagar", "Adicionar filho…", "Reparent…",
        "＋ Luz 2D"
    )
    OniDialog.list(this, currentEntityName(packed), items, dangerIndex = 2) { which ->
        when (which) {
            0 -> inputDialog("Novo nome", currentEntityName(packed)) { name ->
                if (!EditorJni.nativeEditorRenameEntity(handle, packed, name)) {
                    toastErr(lastErrorText())
                }
                refreshPanel()
            }
            1 -> {
                val dup = EditorJni.nativeEditorDuplicateEntity(handle, packed)
                if (dup == 0L) toastErr(lastErrorText()) else selectEntity(dup)
            }
            2 -> if (EditorJni.nativeEditorDeleteEntity(handle, packed)) {
                if (selection == packed) selection = 0L
                refreshPanel()
            } else {
                toastErr(lastErrorText())
            }
            3 -> inputDialog("Nome do filho", "Child") { name ->
                val child = EditorJni.nativeEditorCreateEntity(handle, name, packed)
                if (child == 0L) toastErr(lastErrorText()) else selectEntity(child)
            }
            4 -> reparentDialog(packed)
            5 -> {
                // P4.3 (Bloco 3): Light2D em 1 toque — o preview do
                // alcance aparece no viewport (anel âmbar).
                if (!EditorJni.nativeEditorAddComponent(
                        handle, packed, "eng::render::Light2D")
                ) {
                    toastErr(lastErrorText())
                } else {
                    selectEntity(packed)
                    toastOk("Luz 2D adicionada — veja o anel no viewport")
                }
            }
        }
    }
}

internal fun EditorActivity.reparentDialog(packed: Long) {
    val act = this
    val tsv = EditorJni.nativeEditorHierarchy(handle) ?: return
    val entries = tsv.lines().filter { it.isNotBlank() }
    val names = entries.map { line ->
        val p = line.split('\t')
        val depth = p.getOrNull(0)?.toIntOrNull() ?: 0
        "${" ".repeat(depth * 2)}${p.getOrNull(1) ?: "?"}"
    }
    val packedIds = entries.map { line ->
        line.split('\t').getOrNull(2)?.toLongOrNull() ?: 0L
    }
    OniDialog.list(this, "Novo pai (raiz = primeiro item)",
        listOf("(raiz)") + names) { which ->
        val newParent = if (which == 0) 0L else packedIds[which - 1]
        if (!EditorJni.nativeEditorReparentEntity(handle, packed, newParent)) {
            toastErr(lastErrorText())
        }
        refreshPanel()
    }
}

/** Adicionar componente COM BUSCA (P0-6): filtra o catálogo ao digitar. */
internal fun EditorActivity.addComponentDialog() {
    val act = this
    // P2 (§2/§14): catálogo ADDÁVEL à entidade (sem os presentes/built-ins)
    // + hint de dependência por tipo. Vem do registro REAL do serializer.
    val tsv = if (selection != 0L) {
        EditorJni.nativeEditorAddableComponents(handle, selection)
    } else {
        EditorJni.nativeEditorComponentCatalog(handle)
    } ?: return
    // TSV: name    dependencyHint.
    val rawNames = tsv.lines().filter { it.isNotBlank() }
        .map { it.split('\t').getOrNull(0) ?: "?" }
    val hints = tsv.lines().filter { it.isNotBlank() }
        .map { it.split('\t').getOrElse(1) { "" } }
    val display = rawNames.mapIndexed { i, raw ->
        val hint = hints.getOrNull(i)?.takeIf { it.isNotBlank() }
        if (hint != null) "${prettyComponent(raw)} — $hint" else prettyComponent(raw)
    }

    val search = Oni.field(this).apply {
        setSingleLine()
        hint = "Buscar componente…"
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
    }
    val list = ListView(this).apply {
        divider = null
        dividerHeight = 0
        selector = android.graphics.drawable.ColorDrawable(0)
    }
    val adapter = object : android.widget.ArrayAdapter<String>(
        this, R.layout.oni_list_item, mutableListOf<String>()
    ) {
        override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
            val text = getItem(position) ?: ""
            val row = Oni.listRow(act)
            row.addView(TextView(act).apply {
                this.text = text
                setTextColor(Oni.TEXT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            row.addView(TextView(act).apply {
                this.text = "›"
                setTextColor(Oni.TEXT_DIM)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            })
            return row
        }
    }
    list.adapter = adapter
    var current: List<String> = rawNames
    fun applyFilter(query: String) {
        current = if (query.isBlank()) {
            rawNames
        } else {
            rawNames.filterIndexed { i, raw ->
                raw.contains(query, ignoreCase = true) ||
                    display[i].contains(query, ignoreCase = true)
            }
        }
        adapter.clear()
        adapter.addAll(current.map { n ->
            val idx = rawNames.indexOf(n)
            display.getOrElse(idx) { n }
        })
    }
    applyFilter("")
    search.addTextChangedListener(object : android.text.TextWatcher {
        override fun afterTextChanged(s: android.text.Editable?) {
            applyFilter(s?.toString() ?: "")
        }
        override fun beforeTextChanged(
            s: CharSequence?, a: Int, b: Int, c: Int
        ) {}
        override fun onTextChanged(
            s: CharSequence?, a: Int, b: Int, c: Int
        ) {}
    })
    val container = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        addView(search, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dp(48)))
        addView(list, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dp(300)).apply {
            topMargin = dp(8)
        })
    }
    var dialog: android.app.Dialog? = null
    list.setOnItemClickListener { _, _, which, _ ->
        if (!EditorJni.nativeEditorAddComponent(handle, selection, current[which])) {
            toastErr(lastErrorText())
        }
        dialog?.dismiss()
        refreshPanel()
    }
    dialog = OniDialog.custom(this, "Adicionar componente", container,
        listOf(OniDialog.Btn("Cancelar", accent = false)))
}

// --- assets: menu/pickers/gerência -------------------------------------------------

internal fun EditorActivity.assetMenuDialog(asset: EditorActivity.AssetEntry) {
    val act = this
    val category = assetCategoryName
    // P4.1 (T4/D7 — AUDITORIA DE UI MORTA): cada item tem um handler
    // EXPLÍCITO (não índice compartilhado). Zero UI morta: ou liga, ou
    // não aparece.
    val items = mutableListOf<String>()
    val actions = mutableListOf<() -> Unit>()
    when (category) {
        "audio" -> {
            items.add("▶ Ouvir / ■ Parar (preview)")
            actions.add {
                // P4.3 (N1): TOGGLE — o 2º toque para a voice.
                val wasPlaying =
                    EditorJni.nativeEditorAudioPreviewPlaying(handle)
                val nowPlaying = toggleAudioPreview(asset.name)
                when {
                    nowPlaying -> toastOk("Preview: ${asset.name}")
                    wasPlaying -> toast("Preview parado")
                    else -> toastErr(lastErrorText())  // start falhou
                }
            }
            items.add("Renomear…")
            actions.add { renameAssetDialog(asset, category) }
            items.add("Mover para…")
            actions.add { moveAssetDialog(asset) }
            items.add("Apagar")
            actions.add { deleteAssetDialog(asset, category) }
        }
        "materials" -> {
            items.add("✎ Editar material…")
            actions.add { editMaterialDialog(asset.name) }
            items.add("Renomear…")
            actions.add { renameAssetDialog(asset, category) }
            items.add("Mover para…")
            actions.add { moveAssetDialog(asset) }
            items.add("Apagar")
            actions.add {
                if (EditorJni.nativeEditorMaterialDelete(handle, asset.name)) {
                    refreshAssets()
                } else {
                    toastErr(lastErrorText())
                }
            }
        }
        "scripts" -> {
            items.add("✎ Abrir no editor de scripts…")
            actions.add { scriptEditorDialog(asset.name) }
            items.add("Renomear…")
            actions.add { renameAssetDialog(asset, category) }
            items.add("Mover para…")
            actions.add { moveAssetDialog(asset) }
            items.add("Apagar")
            actions.add { deleteAssetDialog(asset, category) }
        }
        "textures" -> {
            items.add("⬒ Aplicar no sprite selecionado")
            actions.add { applyTextureToSelection(asset.name) }
            items.add("Renomear…")
            actions.add { renameAssetDialog(asset, category) }
            items.add("Mover para…")
            actions.add { moveAssetDialog(asset) }
            items.add("Apagar")
            actions.add { deleteAssetDialog(asset, category) }
        }
        else -> {
            // Categorias sem operação específica: gerência básica
            // (todas ligadas de verdade).
            items.add("Renomear…")
            actions.add { renameAssetDialog(asset, category) }
            items.add("Mover para…")
            actions.add { moveAssetDialog(asset) }
            items.add("Apagar")
            actions.add { deleteAssetDialog(asset, category) }
        }
    }
    val danger = items.indexOf("Apagar")
    OniDialog.list(this, asset.name, items, dangerIndex = danger) { which ->
        if (which in actions.indices) actions[which]()
    }
}

/** P4.1 (D7): renomear asset — extraído (o menu antigo trocava os
 *  índices entre categorias: o defeito D7). */
internal fun EditorActivity.renameAssetDialog(asset: EditorActivity.AssetEntry, category: String) {
    val act = this
    inputDialog("Novo nome", asset.name) { name ->
        if (!EditorJni.nativeEditorAssetRename(handle, category, asset.name, name)) {
            toastErr(lastErrorText())
        }
        refreshAssets()
    }
}

/** P4.1 (D7): apagar asset com confirmação honesta. */
internal fun EditorActivity.deleteAssetDialog(asset: EditorActivity.AssetEntry, category: String) {
    val act = this
    OniDialog.dangerConfirm(
        this, "Apagar ${asset.name}?",
        "O arquivo é removido do projeto (sem undo)."
    ) {
        if (EditorJni.nativeEditorAssetDelete(handle, category, asset.name)) {
            refreshAssets()
        } else {
            toastErr(lastErrorText())
        }
    }
}

/** P4.1 (D7): aplica textura ao SpriteData da entidade selecionada
 *  (via Inspector — MESMO caminho do campo de textura). */
internal fun EditorActivity.applyTextureToSelection(textureName: String) {
    val act = this
    if (selection == 0L) {
        toastErr("Nenhuma entidade selecionada")
        return
    }
    val ok = EditorJni.nativeEditorSetComponentField(
        handle, selection, "eng::editor::SpriteData", "textureAsset",
        textureName
    )
    if (!ok) {
        toastErr(lastErrorText())
    } else {
        toastOk("Textura '$textureName' aplicada")
        refreshInspectorIfOpen()
    }
}

internal fun EditorActivity.moveAssetDialog(asset: EditorActivity.AssetEntry) {
    val act = this
    val cats = (EditorJni.nativeEditorAssetCategories(handle) ?: "")
        .lines().filter { it.isNotBlank() }
    OniDialog.list(this, "Mover para", cats) { which ->
        if (!EditorJni.nativeEditorAssetMove(handle, assetCategoryName, asset.name, cats[which])) {
            toastErr(lastErrorText())
        }
        refreshAssets()
    }
}

/** Preview do asset: imagem REAL decodificada em bitmap (thumbnails
 *  nativos do Android — evolução P0: duplo-toque MOSTRA o conteúdo). */
internal fun EditorActivity.assetPreviewDialog(entry: EditorActivity.AssetEntry) {
    val act = this
    val category = assetCategoryName
    val info = EditorJni.nativeEditorAssetImageInfo(handle, category, entry.name)
    val project = EditorJni.nativeEditorProjectName(handle) ?: ""
    val file = File(File(File(filesDir, "projects"), project),
                    "assets/$category/${entry.name}")
    val content = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
    }
    if (info != null && file.isFile) {
        val bitmap = android.graphics.BitmapFactory.decodeFile(file.absolutePath)
        if (bitmap != null) {
            content.addView(ImageView(this).apply {
                adjustViewBounds = true
                scaleType = ImageView.ScaleType.FIT_CENTER
                setImageBitmap(bitmap)
                background = Oni.rounded(act, Oni.CODE_BG, Oni.R_THUMB)
                setPadding(dp(12), dp(12), dp(12), dp(12))
            })
        }
    }
    content.addView(TextView(this).apply {
        val message = StringBuilder(
            "id: ${entry.id}\nregistrado: ${entry.registered}\ncaminho: ${entry.path}")
        if (info != null) {
            message.append("\nimagem: $info")
        }
        text = message.toString()
        setTextColor(Oni.TEXT_DIM)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
        typeface = Typeface.MONOSPACE
        setPadding(0, dp(10), 0, 0)
    })
    OniDialog.custom(this, entry.name, content,
        listOf(OniDialog.Btn("OK")))
}

// --- pickers do Inspector -----------------------------------------------------------

/** kind="material" — picker dos assets/materials (P3 §3). */
internal fun EditorActivity.pickMaterialFor(
    component: String, path: String, onApplied: (String) -> Unit
) {
    val act = this
    val tsv = EditorJni.nativeEditorListMaterials(handle)
    val names = (tsv ?: "").lines().filter { it.isNotBlank() }
    val options = mutableListOf<String>()
    options.add("")  // (default lit neutro)
    options.addAll(names)
    val labels = options.map { it.ifEmpty { "(default lit)" } }
    OniDialog.list(this, "Material do sprite", labels) { which ->
        setFieldQuiet(component, path, options[which])
        onApplied(options[which])
    }
    // "Novo…" → entrada dedicada (o input do meio da lista vira fluxo).
    // (o picker mantém (default lit) + materiais; criar via Assets.)
}

/** Edição de material (Assets → materials, long-press): shader + cor.
 *  Escreve via materialWrite (valida no codec — lixo não entra). */
internal fun EditorActivity.editMaterialDialog(name: String) {
    val act = this
    val json = EditorJni.nativeEditorMaterialRead(handle, name)
    if (json == null) {
        toastErr(lastErrorText())
        return
    }
    // Estado atual (parse leve do JSON estável do codec).
    var shader = "lit"
    var tintR = 1f; var tintG = 1f; var tintB = 1f; var tintA = 1f
    Regex("\"shader\"\\s*:\\s*\"([^\"]+)\"").find(json)?.let {
        shader = it.groupValues[1]
    }
    Regex("\"tint\"\\s*:\\s*\\[([\\d.eE+-]+),\\s*([\\d.eE+-]+),\\s*([\\d.eE+-]+),\\s*([\\d.eE+-]+)\\]").find(json)?.let {
        tintR = it.groupValues[1].toFloatOrNull() ?: 1f
        tintG = it.groupValues[2].toFloatOrNull() ?: 1f
        tintB = it.groupValues[3].toFloatOrNull() ?: 1f
        tintA = it.groupValues[4].toFloatOrNull() ?: 1f
    }
    val layout = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
    }
    val shaderLabel = TextView(this).apply {
        text = "Shader: ${if (shader == "unlit") "unlit" else "lit"}"
        setTextColor(Oni.TEXT)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        typeface = Typeface.MONOSPACE
        setPadding(0, dp(4), 0, dp(8))
    }
    val alphaEdit = Oni.field(this, mono = true).apply {
        setSingleLine()
        inputType = InputType.TYPE_CLASS_NUMBER or
            InputType.TYPE_NUMBER_FLAG_DECIMAL
        setText(String.format("%.2f", tintA))
        hint = "Alfa do tint (0-1)"
    }
    layout.addView(shaderLabel)
    layout.addView(alphaEdit, LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT, dp(48)))
    OniDialog.custom(
        this, "Material $name", layout,
        listOf(
            OniDialog.Btn("Cancelar", accent = false),
            // Alterna lit/unlit e REABRE o diálogo (fluxo simples).
            OniDialog.Btn("Shader") {
                val toggled = if (shader == "lit") "unlit" else "lit"
                val newJson = "{" +
                    "\"name\": \"${name.removeSuffix(".mat.json")}\", " +
                    "\"shader\": \"$toggled\", " +
                    "\"tint\": [${fmtFloat(tintR)}, ${fmtFloat(tintG)}, " +
                    "${fmtFloat(tintB)}, ${fmtFloat(tintA)}]}"
                if (EditorJni.nativeEditorMaterialWrite(handle, name, newJson)) {
                    toast("Shader: $toggled")
                    refreshAssets()
                    editMaterialDialog(name)
                } else toastErr(lastErrorText())
            },
            OniDialog.Btn("Salvar") {
                val newAlpha = alphaEdit.text.toString().toFloatOrNull()
                if (newAlpha != null && newAlpha >= 0f && newAlpha <= 1f) {
                    tintA = newAlpha
                }
                // Escreve pelo CAMINHO NATIVO (a cor chega via swatch = JSON):
                val newJson = "{" +
                    "\"name\": \"${name.removeSuffix(".mat.json")}\", " +
                    "\"shader\": \"$shader\", " +
                    "\"tint\": [${fmtFloat(tintR)}, ${fmtFloat(tintG)}, " +
                    "${fmtFloat(tintB)}, ${fmtFloat(tintA)}]}"
                if (EditorJni.nativeEditorMaterialWrite(handle, name, newJson)) {
                    toastOk("Material salvo")
                    refreshAssets()
                } else toastErr(lastErrorText())
            }
        )
    )
}

/**
 * Picker de textura COM THUMBNAILS (P0-6): lista os assets de textura do
 * projeto com preview real e atribui no campo genérico.
 */
internal fun EditorActivity.pickTextureFor(
    component: String, path: String, onApplied: (String) -> Unit
) {
    val act = this
    val tsv = EditorJni.nativeEditorListTextures(handle) ?: return
    val names = tsv.lines().filter { it.isNotBlank() }
    if (names.isEmpty()) {
        toastErr("Nenhuma textura importada (Assets → textures → Importar)")
        return
    }
    val list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
    val scroll = ScrollView(this).apply { addView(list) }
    var picker: android.app.Dialog? = null
    for ((i, name) in names.withIndex()) {
        val row = Oni.listRow(this)
        thumbnailOf("textures", name)?.let { bmp ->
            row.addView(
                ImageView(this).apply {
                    setImageBitmap(bmp)
                    scaleType = ImageView.ScaleType.FIT_CENTER
                    background = Oni.rounded(act, Oni.RAISED, Oni.R_THUMB)
                    clipToOutline = true
                },
                LinearLayout.LayoutParams(dp(48), dp(48))
            )
            row.addView(
                View(this),
                LinearLayout.LayoutParams(dp(10), dp(1))
            )
        }
        row.addView(
            TextView(this).apply {
                text = name
                setTextColor(Oni.TEXT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            },
            LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        )
        row.setOnClickListener {
            setFieldQuiet(component, path, name)
            onApplied(name)
            picker?.dismiss()
        }
        list.addView(
            row,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
            )
        )
        if (i < names.size - 1) {
            list.addView(View(this), LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(6)))
        }
    }
    picker = OniDialog.custom(this, "Textura", scroll,
        listOf(OniDialog.Btn("Cancelar", accent = false)))
}

/** Picker de áudio (P2 §12): WAVs de assets/audio. */
internal fun EditorActivity.pickAudioFor(
    component: String, path: String, onApplied: (String) -> Unit
) {
    val act = this
    val tsv = EditorJni.nativeEditorListAudio(handle) ?: return
    val names = tsv.lines().filter { it.isNotBlank() }
    if (names.isEmpty()) {
        toastErr("Nenhum áudio importado (Assets → audio → Importar)")
        return
    }
    OniDialog.list(this, "Áudio", names) { which ->
        setFieldQuiet(component, path, names[which])
        onApplied(names[which])
    }
}

/**
 * Editor de cor REAL (P0-6): sliders R/G/B (+A quando o grupo tem 4
 * canais) com preview ao vivo + hex — nativo, sem dependências.
 * P4.5: sliders/thumb curvos (Oni.slider).
 */
internal fun EditorActivity.colorPickerDialog(
    component: String, path: String, initialHex: String, initialArgb: Int,
    onApplied: (String, Int) -> Unit
) {
    val act = this
    val hasAlpha = initialHex.length == 9
    var r = (initialArgb shr 16) and 0xFF
    var g = (initialArgb shr 8) and 0xFF
    var b = initialArgb and 0xFF
    var a = (initialArgb shr 24) and 0xFF

    val container = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
    }
    val preview = TextView(this).apply {
        text = initialHex
        setTextColor(Oni.TEXT)
        gravity = Gravity.CENTER
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        typeface = Typeface.MONOSPACE
        minimumHeight = dp(56)
        background = Oni.rounded(act, Oni.CODE_BG, Oni.R_FIELD)
    }
    container.addView(preview)

    fun currentArgb(): Int =
        (if (hasAlpha) (a shl 24) else 0xFF shl 24) or (r shl 16) or (g shl 8) or b

    fun currentHex(): String = String.format(
        if (hasAlpha) "#%02X%02X%02X%02X" else "#%02X%02X%02X",
        r, g, b, a
    )

    fun refresh() {
        preview.text = currentHex()
        preview.setBackgroundColor(currentArgb())
        preview.setTextColor(
            if (r * 299 + g * 587 + b * 114 < 128 * 1000)
                0xFFFFFFFF.toInt() else 0xFF000000.toInt()
        )
    }

    fun slider(label: String, init: Int, on: (Int) -> Unit): LinearLayout {
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        row.addView(
            TextView(this).apply {
                text = label
                setTextColor(Oni.TEXT_DIM)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
                typeface = Typeface.MONOSPACE
                width = dp(28)
                gravity = Gravity.CENTER_VERTICAL
            },
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, dp(48)
            )
        )
        row.addView(
            Oni.slider(this).apply {
                max = 255
                progress = init
                setOnSeekBarChangeListener(
                    object : SeekBar.OnSeekBarChangeListener {
                        override fun onProgressChanged(
                            s: SeekBar?, p: Int, fromUser: Boolean
                        ) { on(p); refresh() }

                        override fun onStartTrackingTouch(s: SeekBar?) {}
                        override fun onStopTrackingTouch(s: SeekBar?) {}
                    }
                )
            },
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(48)
            )
        )
        return row
    }

    container.addView(slider("R", r) { r = it })
    container.addView(slider("G", g) { g = it })
    container.addView(slider("B", b) { b = it })
    if (hasAlpha) {
        container.addView(slider("A", a) { a = it })
    }
    refresh()

    OniDialog.custom(
        this, prettyFieldLabel(path), container,
        listOf(
            OniDialog.Btn("Cancelar", accent = false),
            OniDialog.Btn("OK") {
                val hex = currentHex()
                setFieldQuiet(component, path, hex)
                onApplied(hex, currentArgb())
            }
        )
    )
}

// --- Ticks & Camadas: diálogos (ADR-051) ---------------------------------------------

internal fun EditorActivity.addLayerDialog() {
    val act = this
    inputDialog("Nome da camada", "UI") { name ->
        if (!EditorJni.nativeEditorLayerAdd(handle, name.trim())) {
            toastErr(lastErrorText())
        }
        refreshTicks()
    }
}

/** Edita timeScale + participação (update/física/render) da camada —
 *  toca o estado REAL da LayerRegistry (ADR-051). */
internal fun EditorActivity.layerEditDialog(row: LayerRow) {
    val act = this
    val container = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
    }
    container.addView(
        TextView(this).apply {
            text = "timeScale (0 = camada pausada)"
            setTextColor(Oni.TEXT_DIM)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
        }
    )
    val tsField = Oni.field(this, mono = true).apply {
        setSingleLine()
        inputType = InputType.TYPE_CLASS_NUMBER or
            InputType.TYPE_NUMBER_FLAG_DECIMAL
        setText(fmtFloat(row.timeScale))
    }
    container.addView(tsField, LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT, dp(48)).apply {
        topMargin = dp(4)
    })
    fun oniSwitchRow(label: String, checked: Boolean): LinearLayout {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(TextView(this@layerEditDialog).apply {
                text = label
                setTextColor(Oni.TEXT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            }, LinearLayout.LayoutParams(0, dp(48), 1f))
        }
        val sw = Oni.switch(this)
        sw.isChecked = checked
        row.addView(sw, LinearLayout.LayoutParams(dp(56), dp(48)))
        row.tag = sw
        return row
    }
    val rowUpdate = oniSwitchRow("Participa do UPDATE", row.update)
    val rowPhysics = oniSwitchRow("Participa da FÍSICA", row.physics)
    val rowRender = oniSwitchRow("Participa do RENDER", row.render)
    container.addView(rowUpdate)
    container.addView(rowPhysics)
    container.addView(rowRender)
    OniDialog.custom(
        this, "Camada: ${row.name}", container,
        listOf(
            OniDialog.Btn("Cancelar", accent = false),
            OniDialog.Btn("Aplicar") {
                val ts = tsField.text.toString().toFloatOrNull() ?: Float.NaN
                val swU = rowUpdate.tag as Switch
                val swF = rowPhysics.tag as Switch
                val swR = rowRender.tag as Switch
                if (!EditorJni.nativeEditorLayerSetTimeScale(handle, row.name, ts) ||
                    !EditorJni.nativeEditorLayerSetParticipation(
                        handle, row.name,
                        swU.isChecked, swF.isChecked, swR.isChecked
                    )
                ) {
                    toastErr(lastErrorText())
                }
                refreshTicks()
            }
        )
    )
}

// --- Animação: menus/diálogos (P2 §8) -------------------------------------------------

/** Menu de contexto da animação: adicionar frame, fps/loop, anexar,
 *  preview, editar JSON, apagar. */
internal fun EditorActivity.animMenuDialog(name: String) {
    val act = this
    val items = listOf(
        "Adicionar frame (textura)…", "FPS / Loop…", "Anexar à seleção…",
        "Preview na seleção", "Editar JSON…", "Apagar"
    )
    OniDialog.list(this, name, items, dangerIndex = 5) { which ->
        when (which) {
            0 -> pickFrameTexture(name)
            1 -> animMetaDialog(name)
            2 -> {
                if (selection == 0L) {
                    toastErr("Selecione uma entidade primeiro")
                } else if (EditorJni.nativeEditorAnimationAssign(
                        handle, selection, name
                    )
                ) {
                    toastOk("Animação anexada — Play executa o clip real")
                    refreshInspectorIfOpen()
                } else {
                    toastErr(lastErrorText())
                }
            }
            3 -> {
                if (selection == 0L) {
                    toastErr("Selecione uma entidade primeiro")
                } else if (EditorJni.nativeEditorPreviewStart(
                        handle, selection, name
                    )
                ) {
                    toastOk("Preview RODANDO — o transform original volta no Stop")
                } else {
                    toastErr(lastErrorText())
                }
            }
            4 -> animJsonEditorDialog(name)
            5 -> if (EditorJni.nativeEditorAnimationDelete(handle, name)) {
                refreshAnim()
            } else {
                toastErr(lastErrorText())
            }
        }
    }
}

/** Frame = textura REAL do projeto (picker com thumbnails — §8). */
internal fun EditorActivity.pickFrameTexture(animName: String) {
    val act = this
    val tsv = EditorJni.nativeEditorListTextures(handle) ?: return
    val names = tsv.lines().filter { it.isNotBlank() }
    if (names.isEmpty()) {
        toastErr("Nenhuma textura importada (Assets → textures → Importar)")
        return
    }
    OniDialog.list(this, "Frame: textura", names) { which ->
        val when_ = EditorJni.nativeEditorAnimationAddFrame(
            handle, animName, names[which]
        )
        if (when_ >= 0f) {
            toastOk("Frame em t=${"%.2f".format(when_)}s")
            refreshAnim()
        } else {
            toastErr(lastErrorText())
        }
    }
}

internal fun EditorActivity.animMetaDialog(name: String) {
    val act = this
    val container = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
    }
    val input = Oni.field(this, mono = true).apply {
        setSingleLine()
        hint = "FPS (1..120)"
        inputType = InputType.TYPE_CLASS_NUMBER
    }
    container.addView(input, LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT, dp(48)))
    val loopRow = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
    }
    loopRow.addView(TextView(this).apply {
        text = "Loop"
        setTextColor(Oni.TEXT)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
    }, LinearLayout.LayoutParams(0, dp(48), 1f))
    val loopCheck = Oni.switch(this)
    loopRow.addView(loopCheck, LinearLayout.LayoutParams(dp(56), dp(48)))
    container.addView(loopRow)
    container.setPadding(0, dp(4), 0, 0)
    OniDialog.custom(
        this, "FPS / Loop", container,
        listOf(
            OniDialog.Btn("Cancelar", accent = false),
            OniDialog.Btn("OK") {
                val fps = input.text.toString().toFloatOrNull() ?: 8f
                if (!EditorJni.nativeEditorAnimationSetMeta(
                        handle, name, loopCheck.isChecked, fps
                    )
                ) {
                    toastErr(lastErrorText())
                }
            }
        )
    )
}

/** Editor JSON cru (round-trip validado no C++ — lixo é rejeitado). */
internal fun EditorActivity.animJsonEditorDialog(name: String) {
    val act = this
    val content = EditorJni.nativeEditorAnimationRead(handle, name) ?: run {
        toastErr(lastErrorText()); return
    }
    val edit = Oni.field(this, mono = true).apply {
        setText(content)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
        minLines = 12
        gravity = Gravity.TOP
    }
    val scroll = ScrollView(this).apply { addView(edit) }
    OniDialog.custom(
        this, name, scroll,
        listOf(
            OniDialog.Btn("Cancelar", accent = false),
            OniDialog.Btn("Salvar") {
                if (!EditorJni.nativeEditorAnimationWrite(
                        handle, name, edit.text.toString())) {
                    toastErr(lastErrorText())
                } else {
                    toastOk("Animação salva")
                }
            }
        )
    )
}

// --- scripts: editor (Bloco A — wrapper temático; Bloco B = janela dedicada) -----

/**
 * Editor de script (P0-7): Compilar com diagnósticos line:col, Anexar à
 * entidade selecionada, Salvar. P4.5: diálogo curvo com campo mono.
 * (Bloco B substitui pela JANELA DEDICADA — ScriptWindow.kt.)
 */
internal fun EditorActivity.scriptEditorDialog(name: String) {
    // P4.5 (Bloco B): JANELA DEDICADA (ScriptWindow) — card curvo, código
    // #0D1117, syntax coloring, numeração, toolbar flutuante, painéis
    // vars/funcs, auto-indent. Mesmo JNI do P0-7.
    ScriptWindow(this).open(name)
}

/** Diagnósticos do Compilar: TSV "1|0" + linhas "line\tcol\tmessage". */
internal fun EditorActivity.showCompileDiags(tsv: String) {
    val act = this
    val lines = tsv.lines()
    val ok = lines.firstOrNull() == "1"
    val title = if (ok) "Compilou" else "Erros de compilação"
    if (ok) {
        OniDialog.message(this, title, "O script compila até bytecode.")
        return
    }
    val rows = lines.drop(1).filter { it.isNotBlank() }
    val list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
    for (row in rows) {
        val p = row.split('\t')
        list.addView(TextView(this).apply {
            text = "${p.getOrNull(0) ?: "?"}:${p.getOrNull(1) ?: "?"}  ${p.getOrNull(2) ?: ""}"
            setTextColor(Oni.DANGER)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            typeface = Typeface.MONOSPACE
            setPadding(dp(4), dp(4), dp(4), dp(4))
        })
    }
    val scroll = ScrollView(this).apply { addView(list) }
    OniDialog.custom(this, title, scroll,
        listOf(OniDialog.Btn("OK")))
}

// --- configurações do projeto (P4.1 T4/D8) --------------------------------------------

/**
 * P4.1 (T4/D8) — CONFIGURAÇÕES REAIS do projeto: nome editável, camadas da
 * cena, timestep da física e estado do backend de áudio (honesto — D6).
 */
internal fun EditorActivity.showProjectSettingsSheet() {
    val act = this
    val content = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
    }
    val nameField = Oni.field(this).apply {
        setSingleLine()
        setText(EditorJni.nativeEditorProjectName(handle) ?: "")
        hint = "Nome do projeto"
    }
    content.addView(sectionTitle("Nome do projeto"))
    content.addView(nameField, LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT, dp(48)))

    // Camadas (LayerSystem da cena): nome + participação em render.
    content.addView(sectionTitle("Camadas da cena"))
    val layersText = TextView(this).apply {
        setTextColor(Oni.TEXT)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
        typeface = Typeface.MONOSPACE
        text = "· GAME (padrão — render + física)"
    }
    content.addView(layersText)

    // Timestep da física + estado do áudio: valores reais do runtime.
    content.addView(sectionTitle("Física e áudio"))
    val runtimeText = TextView(this).apply {
        setTextColor(Oni.TEXT)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
        typeface = Typeface.MONOSPACE
        text = "Física: timestep fixo 1/60 s (acumulador)\n" +
            "Áudio: ${EditorJni.nativeEditorAudioStatus(handle) ?: "off"}"
    }
    content.addView(runtimeText)

    OniDialog.custom(
        this, "Configurações do projeto", content,
        listOf(
            OniDialog.Btn("Fechar", accent = false),
            OniDialog.Btn("Salvar nome") {
                val newName = nameField.text.toString().trim()
                if (newName.isNotEmpty() &&
                    !EditorJni.nativeEditorSetProjectName(handle, newName)) {
                    toastErr(lastErrorText())
                } else if (newName.isNotEmpty()) {
                    refreshAll()
                }
            }
        )
    )
}
