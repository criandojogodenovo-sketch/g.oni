package com.goni.runtime

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.text.InputType
import android.util.TypedValue
import android.view.GestureDetector
import android.view.Gravity
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.Choreographer
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import java.io.File

/**
 * Editor NATIVO do G.ONI (FASE 8) — touch-first, sem lógica de engine.
 *
 * Decisões (ADR-042):
 * - Views nativos (android.widget) — SEM Compose/Flutter/Web (3,9 GB RAM,
 *   2 núcleos, zero dependências de terceiros);
 * - alvos de toque >= 48dp, botões grandes, painéis em folha inferior;
 * - viewport = SurfaceView renderizado pelo C++ (RHI → Vulkan/GLES) —
 *   nenhum render em Kotlin (missão §2);
 * - loop de frame na UI thread via Choreographer (ADR-039 — igual FASE 7);
 * - gestos do EDITOR vão direto ao documento (§6.4: separados do input
 *   do JOGO que nasce na FASE 9).
 */
class EditorActivity : Activity(), SurfaceHolder.Callback2,
    Choreographer.FrameCallback {

    private var handle: Long = 0L
    private var choreographer: Choreographer? = null
    private var surfaceReady = false
    private var lastFrameNanos = 0L

    // UI
    private lateinit var surfaceView: SurfaceView
    private lateinit var panelContainer: LinearLayout
    private lateinit var btnPlay: Button
    private lateinit var btnProject: Button
    private lateinit var btnTool: Button
    private lateinit var hierarchyList: ListView
    private lateinit var inspectorScroll: ScrollView
    private lateinit var assetCategory: Spinner
    private lateinit var assetList: ListView
    private lateinit var hierarchyAdapter: HierarchyAdapter
    private lateinit var assetAdapter: AssetAdapter

    private var moveToolActive = false
    private var activePanel = PANEL_NONE

    private var importTmpDir: File? = null

    // --- ciclo de vida ---------------------------------------------------------

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        // Workspace: filesDir/projects (interno — sem permissões). Paths
        // DENTRO do projeto seguem relativos (§8.1 — ADR-032).
        val workspace = File(filesDir, "projects").apply { mkdirs() }
        handle = EditorJni.nativeEditorCreate("auto", workspace.absolutePath)
        if (handle == 0L) {
            toast("Falha ao criar o editor (ver logcat)")
            finish()
            return
        }

        buildUi()
        ensureProjectOnFirstRun()
        refreshAll()
    }

    override fun onResume() {
        super.onResume()
        if (handle != 0L) {
            EditorJni.nativeEditorOnResume(handle)
        }
        lastFrameNanos = 0L
        choreographer = Choreographer.getInstance().also { it.postFrameCallback(this) }
    }

    override fun onPause() {
        choreographer?.removeFrameCallback(this)
        choreographer = null
        if (handle != 0L) {
            EditorJni.nativeEditorOnPause(handle)
        }
        super.onPause()
    }

    override fun onDestroy() {
        if (handle != 0L) {
            EditorJni.nativeEditorDestroy(handle)
            handle = 0L
        }
        super.onDestroy()
    }

    // --- UI (programática — sem XML, sem dependências) ---------------------------

    private fun dp(v: Int): Int =
        TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, v.toFloat(), resources.displayMetrics
        ).toInt()

    private fun bigButton(label: String, onClick: (Button) -> Unit): Button {
        val b = Button(this)
        b.text = label
        b.minHeight = dp(48)
        b.minWidth = dp(64)
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
        b.setOnClickListener { onClick(b) }
        return b
    }

    private fun buildUi() {
        // Viewport (fundo) + chrome por cima.
        surfaceView = SurfaceView(this)
        surfaceView.holder.addCallback(this)
        surfaceView.holder.setKeepScreenOn(true)
        attachGestures(surfaceView)

        val root = FrameLayout(this)

        // Barra superior: projeto, cena, PLAY/STOP, backend, ferramenta.
        val top = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(0xE6101216.toInt())
            setPadding(dp(6), dp(4), dp(6), dp(4))
        }
        btnProject = bigButton("Projeto") { showProjectMenu() }
        top.addView(
            btnProject,
            LinearLayout.LayoutParams(0, dp(52), 1.4f)
        )
        top.addView(
            bigButton("Cena") { showSceneMenu() },
            LinearLayout.LayoutParams(0, dp(52), 1f)
        )
        btnPlay = bigButton("▶") { togglePlay() }
        btnPlay.setTextColor(0xFF4CAF50.toInt())
        top.addView(btnPlay, LinearLayout.LayoutParams(0, dp(52), 0.7f))
        top.addView(
            bigButton("Auto") { showBackendMenu(it as Button) },
            LinearLayout.LayoutParams(0, dp(52), 0.8f)
        )
        btnTool = bigButton("PAN") { toggleMoveTool() }
        top.addView(btnTool, LinearLayout.LayoutParams(0, dp(52), 0.7f))

        // Barra inferior: seletor de painéis.
        val bottom = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(0xE6101216.toInt())
            setPadding(dp(6), dp(2), dp(6), dp(2))
        }
        bottom.addView(
            bigButton("Hierarquia") { togglePanel(PANEL_HIERARCHY) },
            LinearLayout.LayoutParams(0, dp(48), 1f)
        )
        bottom.addView(
            bigButton("Inspector") { togglePanel(PANEL_INSPECTOR) },
            LinearLayout.LayoutParams(0, dp(48), 1f)
        )
        bottom.addView(
            bigButton("Assets") { togglePanel(PANEL_ASSETS) },
            LinearLayout.LayoutParams(0, dp(48), 1f)
        )

        // Painel (folha inferior, oculto por padrão).
        panelContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(0xF20E1013.toInt())
            visibility = View.GONE
        }

        root.addView(
            surfaceView,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
        root.addView(
            panelContainer,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(300),
                Gravity.BOTTOM
            ).apply { bottomMargin = dp(52) }
        )
        root.addView(
            bottom,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM
            )
        )
        root.addView(
            top,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP
            )
        )

        hierarchyAdapter = HierarchyAdapter()
        inspectorScroll = ScrollView(this)
        buildAssetsPanel()

        setContentView(root)
    }

    // --- painéis ---------------------------------------------------------------

    private fun togglePanel(panel: Int) {
        if (activePanel == panel) {
            activePanel = PANEL_NONE
            panelContainer.visibility = View.GONE
            return
        }
        activePanel = panel
        panelContainer.removeAllViews()
        when (panel) {
            PANEL_HIERARCHY -> buildHierarchyPanel()
            PANEL_INSPECTOR -> buildInspectorPanel()
            PANEL_ASSETS -> panelContainer.addView(assetsRoot)
        }
        panelContainer.visibility = View.VISIBLE
        refreshPanel()
    }

    private fun refreshPanel() {
        when (activePanel) {
            PANEL_HIERARCHY -> refreshHierarchy()
            PANEL_INSPECTOR -> refreshInspector()
            PANEL_ASSETS -> refreshAssets()
        }
    }

    private fun buildHierarchyPanel() {
        val bar = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        bar.addView(
            bigButton("+ Entidade") { createEntityDialog() },
            LinearLayout.LayoutParams(0, dp(48), 1f)
        )
        hierarchyList = ListView(this).apply {
            adapter = hierarchyAdapter
            onItemClickListener =
                AdapterView.OnItemClickListener { _, _, _, _ ->
                    val packed = hierarchyAdapter.selectedAt()
                    if (packed != 0L) {
                        EditorJni.nativeEditorViewportTap(handle, -1e6f, -1e6f) // (sem hit)
                        selectEntity(packed)
                    }
                }
            onItemLongClickListener =
                AdapterView.OnItemLongClickListener { _, _, _, _ ->
                    val packed = hierarchyAdapter.selectedAt()
                    if (packed != 0L) {
                        entityMenuDialog(packed)
                    }
                    true
                }
        }
        panelContainer.addView(
            bar,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
            )
        )
        panelContainer.addView(
            hierarchyList,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
    }

    private fun buildInspectorPanel() {
        panelContainer.addView(
            inspectorScroll,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
    }

    private lateinit var assetsRoot: LinearLayout

    private fun buildAssetsPanel() {
        assetsRoot = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val bar = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        assetCategory = Spinner(this).apply {
            adapter = ArrayAdapter(
                this@EditorActivity,
                android.R.layout.simple_spinner_dropdown_item,
                mutableListOf<String>()
            )
            onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(
                    p: AdapterView<*>?, v: View?, pos: Int, id: Long
                ) { refreshAssets() }

                override fun onNothingSelected(p: AdapterView<*>?) {}
            }
        }
        bar.addView(
            assetCategory,
            LinearLayout.LayoutParams(0, dp(48), 1.4f)
        )
        bar.addView(
            bigButton("Importar") { pickImportFile() },
            LinearLayout.LayoutParams(0, dp(48), 1f)
        )
        assetAdapter = AssetAdapter()
        assetList = ListView(this).apply {
            adapter = assetAdapter
            onItemLongClickListener =
                AdapterView.OnItemLongClickListener { _, _, _, _ ->
                    assetAdapter.selectedOrNull()?.let { assetMenuDialog(it) }
                    true
                }
        }
        assetsRoot.addView(
            bar,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
            )
        )
        assetsRoot.addView(
            assetList,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
    }

    // --- atualização de dados (snapshots TSV do C++) ------------------------------

    private fun selectEntity(packed: Long) {
        selection = packed
        refreshHierarchy()
        refreshInspector()
    }

    private var selection: Long = 0L

    private fun refreshHierarchy() {
        val tsv = EditorJni.nativeEditorHierarchy(handle) ?: return
        hierarchyAdapter.reload(tsv)
        if (selection != 0L) {
            hierarchyAdapter.markSelected(selection)
        }
        hierarchyAdapter.notifyDataSetChanged()
    }

    private fun refreshInspector() {
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(12), dp(8), dp(12), dp(16))
        }
        if (handle == 0L) {
            inspectorScroll.removeAllViews()
            return
        }
        if (selection == 0L) {
            content.addView(labelView("Nenhuma entidade selecionada\n(toca no viewport ou na hierarquia)"))
            inspectorScroll.removeAllViews()
            inspectorScroll.addView(content)
            return
        }

        // Cabeçalho: nome da entidade (editável).
        val nameField = EditText(this).apply {
            setSingleLine()
            setText(currentEntityName(selection))
            hint = "Nome"
            imeOptions = android.view.inputmethod.EditorInfo.IME_ACTION_DONE
            setOnEditorActionListener { _, actionId, _ ->
                if (actionId == android.view.inputmethod.EditorInfo.IME_ACTION_DONE) {
                    val ok = EditorJni.nativeEditorRenameEntity(handle, selection, text.toString())
                    if (!ok) toast(lastErrorText())
                    refreshHierarchy()
                    true
                } else false
            }
        }
        content.addView(nameField)

        // Transform (TRS com Euler em graus — API do documento).
        val tr = EditorJni.nativeEditorGetTransform(handle, selection)
        if (tr != null && tr.size == 9) {
            content.addView(sectionTitle("Transform"))
            addVec3Row(content, "Posição", tr[0], tr[1], tr[2]) { v ->
                EditorJni.nativeEditorSetTransform(
                    handle, selection,
                    v[0], v[1], v[2], tr[3], tr[4], tr[5], tr[6], tr[7], tr[8]
                )
            }
            addVec3Row(content, "Rotação (°)", tr[3], tr[4], tr[5]) { v ->
                EditorJni.nativeEditorSetTransform(
                    handle, selection,
                    tr[0], tr[1], tr[2], v[0], v[1], v[2], tr[6], tr[7], tr[8]
                )
            }
            addVec3Row(content, "Escala", tr[6], tr[7], tr[8]) { v ->
                EditorJni.nativeEditorSetTransform(
                    handle, selection,
                    tr[0], tr[1], tr[2], tr[3], tr[4], tr[5], v[0], v[1], v[2]
                )
            }
        }

        // Componentes (catálogo reflect-driven — §8.4).
        val componentsTsv = EditorJni.nativeEditorEntityComponents(handle, selection)
        if (componentsTsv != null) {
            for (line in componentsTsv.lines().filter { it.isNotBlank() }) {
                val parts = line.split('\t')
                if (parts.size < 2) continue
                val component = parts[0]
                val removable = parts[1] == "1"
                content.addView(sectionTitle(component))
                val fieldsTsv =
                    EditorJni.nativeEditorComponentFields(handle, selection, component)
                if (fieldsTsv != null) {
                    for (fline in fieldsTsv.lines().filter { it.isNotBlank() }) {
                        val fp = fline.split('\t')
                        if (fp.size < 3) continue
                        addFieldRow(content, component, fp[0], fp[1], fp[2])
                    }
                }
                if (removable) {
                    content.addView(
                        bigButton("Remover $component") {
                            val ok = EditorJni.nativeEditorRemoveComponent(handle, selection, component)
                            if (!ok) toast(lastErrorText())
                            refreshPanel()
                        },
                        LinearLayout.LayoutParams(
                            ViewGroup.LayoutParams.MATCH_PARENT, dp(48)
                        )
                    )
                }
            }
        }
        content.addView(
            bigButton("+ Adicionar componente") { addComponentDialog() },
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(52))
        )

        inspectorScroll.removeAllViews()
        inspectorScroll.addView(content)
    }

    private fun refreshAssets() {
        if (handle == 0L) return
        val cats = EditorJni.nativeEditorAssetCategories(handle) ?: return
        val catList = cats.lines().filter { it.isNotBlank() }
        val adapter = assetCategory.adapter as ArrayAdapter<String>
        if (adapter.count != catList.size) {
            adapter.clear()
            adapter.addAll(catList)
            assetCategory.setSelection(0)
        }
        val category = catList.getOrNull(assetCategory.selectedItemPosition) ?: return
        val tsv = EditorJni.nativeEditorAssetList(handle, category)
        assetAdapter.reload(tsv ?: "")
        assetAdapter.notifyDataSetChanged()
    }

    private fun refreshAll() {
        refreshHierarchySafe()
        if (::btnProject.isInitialized && handle != 0L) {
            val name = EditorJni.nativeEditorProjectName(handle) ?: ""
            btnProject.text = name.ifEmpty { "Projeto" }
        }
    }

    private fun refreshHierarchySafe() {
        if (::hierarchyAdapter.isInitialized && activePanel == PANEL_HIERARCHY) {
            refreshHierarchy()
        }
    }

    // --- helpers de UI ----------------------------------------------------------

    private fun labelView(text: String): TextView =
        TextView(this).apply {
            this.text = text
            setTextColor(Color.LTGRAY)
            setPadding(dp(8), dp(12), dp(8), dp(12))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        }

    private fun sectionTitle(text: String): TextView =
        TextView(this).apply {
            this.text = text
            setTextColor(0xFF8AB4F8.toInt())
            setPadding(dp(4), dp(10), dp(4), dp(2))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        }

    private fun addVec3Row(
        parent: LinearLayout, title: String, x: Float, y: Float, z: Float,
        apply: (FloatArray) -> Unit
    ) {
        parent.addView(labelView(title))
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val fields = mutableListOf<EditText>()
        for (value in listOf(x, y, z)) {
            val edit = EditText(this).apply {
                inputType = InputType.TYPE_CLASS_NUMBER or
                    InputType.TYPE_NUMBER_FLAG_SIGNED or
                    InputType.TYPE_NUMBER_FLAG_DECIMAL
                setSingleLine()
                setText(fmtFloat(value))
                imeOptions = android.view.inputmethod.EditorInfo.IME_ACTION_DONE
                setOnEditorActionListener { _, actionId, _ ->
                    if (actionId == android.view.inputmethod.EditorInfo.IME_ACTION_DONE) {
                        val values = fields.map { it.text.toString().toFloatOrNull() ?: 0f }
                        apply(values.toFloatArray())
                        refreshHierarchySafe()
                        true
                    } else false
                }
            }
            fields.add(edit)
            row.addView(
                edit,
                LinearLayout.LayoutParams(0, dp(48), 1f)
            )
        }
        parent.addView(row)
    }

    private fun addFieldRow(
        parent: LinearLayout, component: String, path: String,
        typeName: String, value: String
    ) {
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        row.addView(
            labelView(path.substringAfterLast('.').let { "$it ($typeName)" }),
            LinearLayout.LayoutParams(0, dp(48), 1f)
        )
        val edit = EditText(this).apply {
            setSingleLine()
            setText(if (typeName == "string") value else value)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            imeOptions = android.view.inputmethod.EditorInfo.IME_ACTION_DONE
            setOnEditorActionListener { _, actionId, _ ->
                if (actionId == android.view.inputmethod.EditorInfo.IME_ACTION_DONE) {
                    val ok = EditorJni.nativeEditorSetComponentField(
                        handle, selection, component, path, text.toString()
                    )
                    if (!ok) toast(lastErrorText())
                    true
                } else false
            }
        }
        row.addView(
            edit,
            LinearLayout.LayoutParams(0, dp(48), 1f)
        )
        parent.addView(row)
    }

    private fun fmtFloat(v: Float): String =
        if (v == v.toLong().toFloat() && kotlin.math.abs(v) < 1e6f) {
            v.toLong().toString()
        } else {
            String.format("%.3f", v)
        }

    private fun currentEntityName(packed: Long): String {
        val tsv = EditorJni.nativeEditorHierarchy(handle) ?: return ""
        for (line in tsv.lines()) {
            val parts = line.split('\t')
            if (parts.size >= 3 && parts[2].toLongOrNull() == packed) {
                return parts[1]
            }
        }
        return ""
    }

    private fun toast(text: String) {
        Toast.makeText(this, text, Toast.LENGTH_SHORT).show()
    }

    private fun lastErrorText(): String =
        EditorJni.nativeEditorLastError(handle) ?: "operação falhou"

    // --- diálogos ------------------------------------------------------------------

    private fun inputDialog(
        title: String, initial: String, onOk: (String) -> Unit
    ) {
        val input = EditText(this).apply {
            setSingleLine()
            setText(initial)
        }
        AlertDialog.Builder(this)
            .setTitle(title)
            .setView(input)
            .setPositiveButton("OK") { _, _ -> onOk(input.text.toString()) }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    private fun ensureProjectOnFirstRun() {
        if (!EditorJni.nativeEditorHasProject(handle)) {
            // Primeira execução: cria o projeto padrão (§8.1).
            if (!EditorJni.nativeEditorNewProject(handle, "MeuJogo")) {
                toast(lastErrorText())
            }
        }
    }

    private fun showProjectMenu() {
        val items = arrayOf(
            "Novo projeto…", "Abrir projeto…", "Salvar projeto",
            "Configurações do projeto…"
        )
        AlertDialog.Builder(this)
            .setTitle("Projeto")
            .setItems(items) { _, which ->
                when (which) {
                    0 -> inputDialog("Nome do novo projeto", "NovoJogo") { name ->
                        if (EditorJni.nativeEditorNewProject(handle, name)) {
                            EditorJni.nativeEditorNewScene(handle)
                            refreshAll()
                        } else toast(lastErrorText())
                    }
                    1 -> openProjectDialog()
                    2 -> if (!EditorJni.nativeEditorSaveProject(handle)) {
                        toast(lastErrorText())
                    }
                    3 -> inputDialog(
                        "Nome do projeto",
                        EditorJni.nativeEditorProjectName(handle) ?: ""
                    ) { name ->
                        if (!EditorJni.nativeEditorSetProjectName(handle, name)) {
                            toast(lastErrorText())
                        } else {
                            refreshAll()
                        }
                    }
                }
            }
            .show()
    }

    private fun openProjectDialog() {
        val workspace = File(filesDir, "projects")
        val projects = workspace.listFiles()?.filter { it.isDirectory } ?: emptyList()
        if (projects.isEmpty()) {
            toast("Nenhum projeto em ${workspace.name}")
            return
        }
        val names = projects.map { it.name }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle("Abrir projeto")
            .setItems(names) { _, which ->
                val name = names[which]
                if (EditorJni.nativeEditorOpenProject(handle, name)) {
                    EditorJni.nativeEditorNewScene(handle)
                    refreshAll()
                } else {
                    toast(lastErrorText())
                }
            }
            .show()
    }

    private fun showSceneMenu() {
        val items = arrayOf("Nova cena", "Salvar cena…", "Carregar cena…")
        AlertDialog.Builder(this)
            .setTitle("Cena")
            .setItems(items) { _, which ->
                when (which) {
                    0 -> EditorJni.nativeEditorNewScene(handle).also {
                        selection = 0L; refreshPanel()
                    }
                    1 -> inputDialog("Salvar cena em (relativo)", "main.json") { path ->
                        if (!EditorJni.nativeEditorSaveScene(handle, path)) {
                            toast(lastErrorText())
                        }
                    }
                    2 -> loadSceneDialog()
                }
            }
            .show()
    }

    private fun loadSceneDialog() {
        val scenesRoot = File(File(filesDir, "projects"), "scenes")
        val files = scenesRoot.listFiles()?.filter { it.isFile } ?: emptyList()
        if (files.isEmpty()) {
            toast("Nenhuma cena salva em scenes/")
            return
        }
        val names = files.map { it.name }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle("Carregar cena")
            .setItems(names) { _, which ->
                if (EditorJni.nativeEditorLoadScene(handle, names[which])) {
                    selection = 0L
                    refreshPanel()
                } else {
                    toast(lastErrorText())
                }
            }
            .show()
    }

    private fun createEntityDialog() {
        inputDialog("Nome da entidade", "Entity") { name ->
            val parent = 0L // raiz (reparent pelo menu de contexto)
            val packed = EditorJni.nativeEditorCreateEntity(handle, name, parent)
            if (packed == 0L) {
                toast(lastErrorText())
            } else {
                selectEntity(packed)
            }
        }
    }

    private fun entityMenuDialog(packed: Long) {
        val items = arrayOf(
            "Renomear…", "Duplicar", "Apagar", "Adicionar filho…", "Reparent…"
        )
        AlertDialog.Builder(this)
            .setTitle(currentEntityName(packed))
            .setItems(items) { _, which ->
                when (which) {
                    0 -> inputDialog("Novo nome", currentEntityName(packed)) { name ->
                        if (!EditorJni.nativeEditorRenameEntity(handle, packed, name)) {
                            toast(lastErrorText())
                        }
                        refreshPanel()
                    }
                    1 -> {
                        val dup = EditorJni.nativeEditorDuplicateEntity(handle, packed)
                        if (dup == 0L) toast(lastErrorText()) else selectEntity(dup)
                    }
                    2 -> if (EditorJni.nativeEditorDeleteEntity(handle, packed)) {
                        if (selection == packed) selection = 0L
                        refreshPanel()
                    } else {
                        toast(lastErrorText())
                    }
                    3 -> inputDialog("Nome do filho", "Child") { name ->
                        val child = EditorJni.nativeEditorCreateEntity(handle, name, packed)
                        if (child == 0L) toast(lastErrorText()) else selectEntity(child)
                    }
                    4 -> reparentDialog(packed)
                }
            }
            .show()
    }

    private fun reparentDialog(packed: Long) {
        val tsv = EditorJni.nativeEditorHierarchy(handle) ?: return
        val entries = tsv.lines().filter { it.isNotBlank() }
        val names = entries.map { line ->
            val p = line.split('\t')
            val depth = p.getOrNull(0)?.toIntOrNull() ?: 0
            "${" ".repeat(depth * 2)}${p.getOrNull(1) ?: "?"}"
        }.toTypedArray()
        val packedIds = entries.map { line ->
            line.split('\t').getOrNull(2)?.toLongOrNull() ?: 0L
        }.toLongArray()
        AlertDialog.Builder(this)
            .setTitle("Novo pai (raiz = primeiro item)")
            .setItems(arrayOf("(raiz)") + names) { _, which ->
                val newParent = if (which == 0) 0L else packedIds[which - 1]
                if (!EditorJni.nativeEditorReparentEntity(handle, packed, newParent)) {
                    toast(lastErrorText())
                }
                refreshPanel()
            }
            .show()
    }

    private fun addComponentDialog() {
        val tsv = EditorJni.nativeEditorComponentCatalog(handle) ?: return
        val entries = tsv.lines().filter { it.isNotBlank() }
        val names = entries.map { it.split('\t').getOrNull(0) ?: "?" }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle("Adicionar componente")
            .setItems(names) { _, which ->
                if (!EditorJni.nativeEditorAddComponent(handle, selection, names[which])) {
                    toast(lastErrorText())
                }
                refreshPanel()
            }
            .show()
    }

    private fun assetMenuDialog(asset: AssetEntry) {
        val items = arrayOf("Renomear…", "Mover para…", "Apagar")
        AlertDialog.Builder(this)
            .setTitle(asset.name)
            .setItems(items) { _, which ->
                when (which) {
                    0 -> inputDialog("Novo nome", asset.name) { name ->
                        if (!EditorJni.nativeEditorAssetRename(handle, assetCategory.selectedItem.toString(), asset.name, name)) {
                            toast(lastErrorText())
                        }
                        refreshAssets()
                    }
                    1 -> moveAssetDialog(asset)
                    2 -> if (EditorJni.nativeEditorAssetDelete(handle, assetCategory.selectedItem.toString(), asset.name)) {
                        refreshAssets()
                    } else {
                        toast(lastErrorText())
                    }
                }
            }
            .show()
    }

    private fun moveAssetDialog(asset: AssetEntry) {
        val cats = (EditorJni.nativeEditorAssetCategories(handle) ?: "")
            .lines().filter { it.isNotBlank() }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle("Mover para")
            .setItems(cats) { _, which ->
                if (!EditorJni.nativeEditorAssetMove(handle, assetCategory.selectedItem.toString(), asset.name, cats[which])) {
                    toast(lastErrorText())
                }
                refreshAssets()
            }
            .show()
    }

    // --- import via SAF (aquisição é papel da plataforma — §D7) ----------------------

    private fun pickImportFile() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
        }
        startActivityForResult(intent, REQUEST_IMPORT)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQUEST_IMPORT || resultCode != RESULT_OK) return
        val uri: Uri = data?.data ?: return

        // Copia para o staging DENTRO do workspace e o C++ só vê eng::fs.
        val tmp = (importTmpDir ?: File(
            File(filesDir, "projects"), ".import_tmp"
        ).also { it.mkdirs(); importTmpDir = it })
        val dest = File(tmp, uri.lastPathSegment?.substringAfterLast('/') ?: "import.bin")
        contentResolver.openInputStream(uri)?.use { input ->
            dest.outputStream().use { output -> input.copyTo(output) }
        } ?: run { toast("Não foi possível ler o arquivo"); return }

        val category = assetCategory.selectedItem?.toString() ?: return
        inputDialog("Nome do asset", dest.nameWithoutExtension) { name ->
            // Path relativo ao ROOT do projeto (workspace/projects/<p>/.import_tmp/x)
            val projectRoot = File(File(filesDir, "projects"),
                EditorJni.nativeEditorProjectName(handle) ?: "")
            val rel = ".import_tmp/${dest.name}"
            if (EditorJni.nativeEditorAssetImport(handle, rel, category, name)) {
                toast("Importado em $category")
                refreshAssets()
            } else {
                toast(lastErrorText())
            }
            (projectRoot) // (caminho base documentado; import usa o rel)
        }
    }

    // --- play/stop (§8.7) ------------------------------------------------------------

    private fun togglePlay() {
        if (EditorJni.nativeEditorIsPlaying(handle)) {
            EditorJni.nativeEditorStop(handle)
            btnPlay.text = "▶"
            btnPlay.setTextColor(0xFF4CAF50.toInt())
            toast("STOP — edição intacta")
        } else {
            if (EditorJni.nativeEditorPlay(handle)) {
                btnPlay.text = "■"
                btnPlay.setTextColor(0xFFFF5252.toInt())
                toast("PLAY — runtime clone ativo")
            } else {
                toast(lastErrorText())
            }
        }
        refreshPanel()
    }

    // --- backend / ferramenta ------------------------------------------------------------

    private fun showBackendMenu(button: Button) {
        val options = arrayOf("auto", "vulkan", "gles")
        AlertDialog.Builder(this)
            .setTitle("Backend de render")
            .setItems(options) { _, which ->
                EditorJni.nativeEditorSetBackend(handle, options[which])
                button.text = options[which]
            }
            .show()
    }

    private fun toggleMoveTool() {
        moveToolActive = !moveToolActive
        btnTool.text = if (moveToolActive) "MOVER" else "PAN"
    }

    // --- gestos do viewport (§8.6/§8.8 — eventos do EDITOR, não do jogo) ------------------

    /**
     * Em PLAY (sem ferramenta ativa), os toques do viewport vão ao INPUT DO
     * JOGO (§6.4); a câmera do editor exige a ferramenta PAN/MOVER —
     * separação explícita editor×jogo.
     */
    private fun gameWantsTouch(): Boolean =
        handle != 0L && EditorJni.nativeEditorIsPlaying(handle) &&
            !moveToolActive

    private fun attachGestures(view: SurfaceView) {
        val scaleDetector = ScaleGestureDetector(
            this,
            object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
                override fun onScale(detector: ScaleGestureDetector): Boolean {
                    EditorJni.nativeEditorViewportZoom(
                        handle, detector.scaleFactor,
                        detector.focusX, detector.focusY
                    )
                    return true
                }
            }
        )
        val tapDetector = GestureDetector(
            this,
            object : GestureDetector.SimpleOnGestureListener() {
                override fun onSingleTapUp(e: MotionEvent): Boolean {
                    // Toques de JOGO são roteados brutos no listener — aqui
                    // só a seleção do EDITOR (bugs C-5/C-6 da auditoria).
                    val hit = EditorJni.nativeEditorViewportTap(handle, e.x, e.y)
                    if (hit != 0L) {
                        selectEntity(hit)
                    } else {
                        selection = 0L
                        refreshPanel()
                    }
                    return true
                }

                override fun onScroll(
                    e1: MotionEvent?, e2: MotionEvent, dx: Float, dy: Float
                ): Boolean {
                    if (moveToolActive && selection != 0L) {
                        // MOVE a entidade selecionada (edit: dirty; play: clone).
                        EditorJni.nativeEditorMoveEntity(handle, selection, dx, dy)
                    } else if (gameWantsTouch()) {
                        EditorJni.nativeEditorGameTouch(handle, 1, 0, e2.x, e2.y, 1f)
                    } else {
                        EditorJni.nativeEditorViewportPan(handle, dx, dy)
                    }
                    return true
                }
            }
        )
        view.setOnTouchListener { _, event ->
            // Bugs C-5/C-6 da auditoria final: em Play SEM ferramenta, os
            // eventos BRUTOS vão ao input do jogo com fases e pointer IDs
            // REAIS (como o GoniActivity) — o tap sintético (Down+Up na
            // mesma janela de update) nunca expunha pressed/down, e o
            // pointerId fixo 0 descartava o multitouch. Os detectores de
            // gesto do EDITOR só rodam fora do modo jogo.
            if (gameWantsTouch()) {
                dispatchGameTouch(event)
                true
            } else {
                scaleDetector.onTouchEvent(event)
                tapDetector.onTouchEvent(event)
                true
            }
        }
    }

    /**
     * Evento bruto → input canônico do jogo: fases 0-3 com pointer ID real
     * (multitouch); ACTION_MOVE é enviado para TODOS os dedos ativos.
     */
    private fun dispatchGameTouch(event: MotionEvent) {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                EditorJni.nativeEditorGameTouch(
                    handle, 0, event.getPointerId(i), event.getX(i),
                    event.getY(i), event.getPressure(i)
                )
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) {
                    EditorJni.nativeEditorGameTouch(
                        handle, 1, event.getPointerId(i), event.getX(i),
                        event.getY(i), event.getPressure(i)
                    )
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val i = event.actionIndex
                EditorJni.nativeEditorGameTouch(
                    handle, 2, event.getPointerId(i), event.getX(i),
                    event.getY(i), event.getPressure(i)
                )
            }
            MotionEvent.ACTION_CANCEL -> {
                for (i in 0 until event.pointerCount) {
                    EditorJni.nativeEditorGameTouch(
                        handle, 3, event.getPointerId(i), event.getX(i),
                        event.getY(i), event.getPressure(i)
                    )
                }
            }
        }
    }

    // --- surface + loop (padrão FASE 7 — ADR-039/040) --------------------------------------

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (handle != 0L) {
            EditorJni.nativeEditorSurfaceCreated(handle, holder.surface)
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        if (handle != 0L) {
            EditorJni.nativeEditorSurfaceChanged(handle, width, height)
            // Bug C-4 da auditoria final: o input do JOGO também precisa do
            // tamanho (zonas de toque em fração da tela — Input.cpp).
            EditorJni.nativeEditorSetGameViewportSize(handle, width, height)
            surfaceReady = width > 0 && height > 0
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        // Cancela toques do jogo ao perder o foco (§6.10 pause robusto).
        if (!hasFocus && handle != 0L) {
            EditorJni.nativeEditorGameTouch(handle, 3, 0, 0f, 0f, 0f)
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceReady = false // nada renderiza daqui em diante (§VIII)
        if (handle != 0L) {
            EditorJni.nativeEditorSurfaceDestroyed(handle)
        }
    }

    override fun surfaceRedrawNeeded(holder: SurfaceHolder) {
        // Choreographer cobre o próximo frame.
    }

    override fun doFrame(nanos: Long) {
        if (handle != 0L && surfaceReady) {
            val delta = if (lastFrameNanos == 0L) 0f
                        else (nanos - lastFrameNanos) / 1e9f
            lastFrameNanos = nanos
            EditorJni.nativeEditorRenderFrame(handle, delta.coerceIn(0f, 0.1f))
        }
        choreographer?.postFrameCallback(this)
    }

    // --- adapters ---------------------------------------------------------------------------

    /** Linha da hierarquia (TSV depth\tname\tpacked). */
    private data class HierarchyRow(val depth: Int, val name: String, val packed: Long)

    private inner class HierarchyAdapter : ArrayAdapter<HierarchyRow>(
        this@EditorActivity, android.R.layout.simple_list_item_1
    ) {
        private val rows = mutableListOf<HierarchyRow>()
        private var selectedPos = -1

        fun reload(tsv: String) {
            rows.clear()
            for (line in tsv.lines().filter { it.isNotBlank() }) {
                val p = line.split('\t')
                if (p.size < 3) continue
                val packed = p[2].toLongOrNull() ?: continue
                rows.add(HierarchyRow(p[0].toIntOrNull() ?: 0, p[1], packed))
            }
            clear()
            addAll(rows)
            selectedPos = -1
        }

        fun markSelected(packed: Long) {
            selectedPos = rows.indexOfFirst { it.packed == packed }
        }

        /** O item "selecionado" para listeners: quem está marcado ou nada. */
        fun selectedAt(): Long =
            if (selectedPos in rows.indices) rows[selectedPos].packed else 0L

        override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
            val view = super.getView(position, convertView, parent)
            val row = rows[position]
            (view as? TextView)?.apply {
                text = "${"  ".repeat(row.depth)}${if (row.packed == selection) "▶ " else ""}${row.name}"
                setTextColor(if (row.packed == selection) 0xFF8AB4F8.toInt() else Color.WHITE)
                setPadding(dp(8) + row.depth * dp(14), dp(12), dp(8), dp(12))
                minHeight = dp(48)
            }
            view.setOnClickListener {
                selectEntity(row.packed)
            }
            view.setOnLongClickListener {
                entityMenuDialog(row.packed)
                true
            }
            return view
        }
    }

    /** Entrada de asset (TSV name\tid\treg\tpath). */
    private data class AssetEntry(
        val name: String, val id: String, val registered: Boolean, val path: String
    )

    private inner class AssetAdapter : ArrayAdapter<AssetEntry>(
        this@EditorActivity, android.R.layout.simple_list_item_1
    ) {
        private val entries = mutableListOf<AssetEntry>()
        private var selected: AssetEntry? = null

        fun reload(tsv: String) {
            entries.clear()
            for (line in tsv.lines().filter { it.isNotBlank() }) {
                val p = line.split('\t')
                if (p.size < 4) continue
                entries.add(
                    AssetEntry(p[0], p[1], p[2] == "1", p[3])
                )
            }
            selected = null
            clear()
            addAll(entries)
        }

        fun selectedOrNull(): AssetEntry? = selected

        override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
            val view = super.getView(position, convertView, parent)
            val entry = entries[position]
            (view as? TextView)?.apply {
                text = if (entry.registered) {
                    "${entry.name}  ·  ${entry.id.take(8)}…"
                } else {
                    "${entry.name}  ·  (não catalogado)"
                }
                setTextColor(if (entry.registered) Color.WHITE else Color.GRAY)
                setPadding(dp(8), dp(12), dp(8), dp(12))
                minHeight = dp(48)
            }
            view.setOnClickListener {
                selected = entry
                AlertDialog.Builder(this@EditorActivity)
                    .setTitle(entry.name)
                    .setMessage(
                        "id: ${entry.id}\nregistrado: ${entry.registered}\n" +
                            "caminho: ${entry.path}"
                    )
                    .setPositiveButton("OK", null)
                    .show()
            }
            view.setOnLongClickListener {
                selected = entry
                assetMenuDialog(entry)
                true
            }
            return view
        }
    }

    companion object {
        private const val PANEL_NONE = 0
        private const val PANEL_HIERARCHY = 1
        private const val PANEL_INSPECTOR = 2
        private const val PANEL_ASSETS = 3
        private const val REQUEST_IMPORT = 4101
    }
}
