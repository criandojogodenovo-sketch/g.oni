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
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.ScrollView
import android.widget.Space
import android.widget.Spinner
import android.widget.Switch
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import android.content.Context
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

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
    private lateinit var topBar: LinearLayout
    private lateinit var bottomBar: LinearLayout
    private lateinit var brand: TextView
    private lateinit var panelHost: FrameLayout
    private lateinit var panelContainer: LinearLayout
    private lateinit var btnPlay: Button
    private lateinit var btnProject: Button
    private lateinit var btnBackend: Button
    private lateinit var btnTool: Button
    private lateinit var hierarchyList: ListView
    private lateinit var inspectorScroll: ScrollView
    private lateinit var assetCategory: Spinner
    private lateinit var assetList: ListView
    private lateinit var hierarchyAdapter: HierarchyAdapter
    private lateinit var assetAdapter: AssetAdapter

    private var editorTool = 0 // 0=Select 1=Move 2=Rotate 3=Scale (C++ manda)
    private var activePanel = PANEL_NONE

    // Live sync (P1.9): últimos valores de transform exibidos no Inspector.
    private var transformFields: MutableList<android.widget.EditText> =
        mutableListOf()
    private var collectTransformFields = false
    private var lastSelectionRevision = -1L

    // Painel de scripts (P0-7): lista carregada por refreshScripts().
    private lateinit var scriptsList: ListView
    private val scriptNames = mutableListOf<String>()

    private lateinit var importButton: Button
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
        // P2 (§5): o DOCUMENTO é a fonte da verdade — a ferramenta da UI
        // sincroniza com a nativa (activity recriada não diverge).
        editorTool = EditorJni.nativeEditorGetTool(handle)
        btnTool.text = toolLabel(editorTool)
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

    // Identidade visual G.ONI (evolução P0-4: dark compact, viewport
    // dominante, densidade de editor moderno — sem copiar Godot/Unreal).
    private object Ui {
        const val BG = 0xFF0B0E13.toInt()        // fundo geral
        const val SURFACE = 0xF211161F.toInt()    // painéis/barras (com alpha)
        const val SURFACE_SOLID = 0xFF11161F.toInt()
        const val SURFACE_ALT = 0xFF161D29.toInt()  // linhas alternadas/hover
        const val BORDER = 0xFF232B3A.toInt()
        const val ACCENT = 0xFF8AB4F8.toInt()     // marca G.ONI
        const val ACCENT_DIM = 0xFF5E8BE0.toInt()
        const val TEXT = 0xFFE6EDF3.toInt()
        const val TEXT_DIM = 0xFF8B949E.toInt()
        const val DANGER = 0xFFFF5252.toInt()
        const val OK = 0xFF4CAF50.toInt()
    }

    private fun dp(v: Int): Int =
        TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, v.toFloat(), resources.displayMetrics
        ).toInt()

    /** Botão compacto do editor (40dp — densidade de ferramenta, não botão
     * de marketing; alvo de toque OK pelo padding interno). */
    private fun toolButton(label: String, onClick: (Button) -> Unit): Button {
        val b = Button(this)
        b.text = label
        b.minHeight = 0
        b.minWidth = 0
        b.setPadding(dp(10), 0, dp(10), 0)
        b.height = dp(36)
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
        b.setTextColor(Ui.TEXT)
        b.isAllCaps = false
        b.background = rippleBox(Ui.SURFACE_ALT, dp(6))
        b.setOnClickListener { onClick(b) }
        return b
    }

    /** Fundo arredondado com ripple (sem lib de terceiros). */
    private fun rippleBox(color: Int, radiusPx: Int): android.graphics.drawable.Drawable {
        val base = android.graphics.drawable.GradientDrawable().apply {
            setColor(color)
            cornerRadius = radiusPx.toFloat()
        }
        return android.graphics.drawable.RippleDrawable(
            android.content.res.ColorStateList.valueOf(0x338AB4F8), base, null
        )
    }

    private fun buildUi() {
        // Viewport (fundo) + chrome por cima.
        surfaceView = SurfaceView(this)
        surfaceView.holder.addCallback(this)
        surfaceView.holder.setKeepScreenOn(true)
        attachGestures(surfaceView)

        val root = FrameLayout(this)
        root.setBackgroundColor(Ui.BG)

        // ---- barra superior: marca + projeto + cena | backend | play ----
        topBar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(Ui.SURFACE)
            setPadding(dp(8), dp(6), dp(8), dp(6))
            gravity = android.view.Gravity.CENTER_VERTICAL
        }
        brand = TextView(this).apply {
            text = "G.ONI"
            setTextColor(Ui.ACCENT)
            typeface = android.graphics.Typeface.DEFAULT_BOLD
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            setPadding(0, 0, dp(8), 0)
        }
        topBar.addView(brand)
        btnProject = toolButton("☰") { showProjectMenu() }
        topBar.addView(
            btnProject,
            LinearLayout.LayoutParams(dp(38), dp(36))
        )
        topBar.addView(
            toolButton("Cena") { showSceneMenu() },
            LinearLayout.LayoutParams(0, dp(36), 0.9f)
        )
        btnPlay = toolButton("▶") { togglePlay() }
        btnPlay.setTextColor(Ui.OK)
        btnPlay.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
        topBar.addView(
            btnPlay,
            LinearLayout.LayoutParams(dp(42), dp(36))
        )
        btnBackend = toolButton("Auto") { showBackendMenu(it as Button) }
        topBar.addView(
            btnBackend,
            LinearLayout.LayoutParams(0, dp(36), 0.7f)
        )
        btnTool = toolButton("FERRAMENTA") { showToolMenu() }
        topBar.addView(
            btnTool,
            LinearLayout.LayoutParams(0, dp(36), 0.9f)
        )

        // ---- barra inferior: toggles de painel (sheets/drawers) ----
        bottomBar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(Ui.SURFACE)
            setPadding(dp(8), dp(5), dp(8), dp(5))
            gravity = android.view.Gravity.CENTER_VERTICAL
        }
        bottomBar.addView(
            toolButton("Hierarquia") { togglePanel(PANEL_HIERARCHY) },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        bottomBar.addView(
            toolButton("Inspector") { togglePanel(PANEL_INSPECTOR) },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        bottomBar.addView(
            toolButton("Assets") { togglePanel(PANEL_ASSETS) },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        bottomBar.addView(
            toolButton("Scripts") { togglePanel(PANEL_SCRIPTS) },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        bottomBar.addView(
            toolButton("Animação") { togglePanel(PANEL_ANIM) },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )

        // ---- host de painel (sheet inferior / drawer lateral) ----
        panelHost = FrameLayout(this)

        root.addView(
            surfaceView,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
        root.addView(
            panelHost,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
        root.addView(
            bottomBar,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                android.view.Gravity.BOTTOM
            )
        )
        root.addView(
            topBar,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                android.view.Gravity.TOP
            )
        )

        panelContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Ui.SURFACE_SOLID)
            visibility = View.GONE
        }

        hierarchyAdapter = HierarchyAdapter()
        inspectorScroll = ScrollView(this)
        buildAssetsPanel()

        setContentView(root)
        applyWindowInsets()
        updatePanelPlacement()
    }

    /** Insets reais (status/nav bar — sem androidx): o chrome RESPETA o
     * sistema em portrait e landscape (evolução P0-4). */
    private fun applyWindowInsets() {
        window.decorView.setOnApplyWindowInsetsListener { _, insets ->
            val top = insets.systemWindowInsetTop
            val bottom = insets.systemWindowInsetBottom
            val left = insets.systemWindowInsetLeft
            val right = insets.systemWindowInsetRight
            topBar.setPadding(dp(8) + left, dp(6) + top, dp(8) + right, dp(6))
            bottomBar.setPadding(dp(8) + left, dp(5), dp(8) + right, dp(5) + bottom)
            updatePanelPlacement()
            insets
        }
    }

    /** Portrait: painel = sheet inferior (máx 62% da altura, viewport
     * continua por trás). Landscape: drawer lateral direito (46%). */
    private fun updatePanelPlacement() {
        if (!::panelHost.isInitialized || !::panelContainer.isInitialized) return
        (panelContainer.parent as? FrameLayout)?.removeView(panelContainer)
        val isLandscape = resources.configuration.orientation ==
                android.content.res.Configuration.ORIENTATION_LANDSCAPE
        if (isLandscape) {
            panelHost.addView(
                panelContainer,
                FrameLayout.LayoutParams(
                    (resources.displayMetrics.widthPixels * 0.46f).toInt(),
                    ViewGroup.LayoutParams.MATCH_PARENT
                ).apply {
                    gravity = android.view.Gravity.RIGHT or android.view.Gravity.BOTTOM
                }
            )
        } else {
            panelHost.addView(
                panelContainer,
                FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    (resources.displayMetrics.heightPixels * 0.62f).toInt(),
                    android.view.Gravity.BOTTOM
                )
            )
        }
    }

    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) {
        super.onConfigurationChanged(newConfig)
        // configChanges cobre orientation|screenSize — o layout ADAPTA em
        // runtime sem recriar a Activity (evolução P0-4: portrait E
        // landscape corretos).
        updatePanelPlacement()
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
        val title = when (panel) {
            PANEL_HIERARCHY -> "Hierarquia"
            PANEL_INSPECTOR -> "Inspector"
            PANEL_ASSETS -> "Assets"
            PANEL_SCRIPTS -> "Scripts"
            PANEL_ANIM -> "Animação"
            else -> ""
        }
        // Cabeçalho do sheet: título + fechar (padrão de drawer moderno).
        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(dp(12), dp(8), dp(4), dp(8))
            gravity = android.view.Gravity.CENTER_VERTICAL
            setBackgroundColor(Ui.SURFACE_ALT)
        }
        header.addView(
            TextView(this).apply {
                text = title
                setTextColor(Ui.TEXT)
                typeface = android.graphics.Typeface.DEFAULT_BOLD
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            },
            LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        )
        header.addView(
            toolButton("✕") { togglePanel(panel) }.apply { setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f) },
            LinearLayout.LayoutParams(dp(36), dp(32))
        )
        panelContainer.addView(
            header,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
        )
        when (panel) {
            PANEL_HIERARCHY -> buildHierarchyPanel()
            PANEL_INSPECTOR -> buildInspectorPanel()
            PANEL_ASSETS -> panelContainer.addView(assetsRoot)
            PANEL_SCRIPTS -> buildScriptsPanel()
            PANEL_ANIM -> buildAnimPanel()
        }
        panelContainer.visibility = View.VISIBLE
        refreshPanel()
    }

    private fun refreshPanel() {
        when (activePanel) {
            PANEL_HIERARCHY -> refreshHierarchy()
            PANEL_INSPECTOR -> refreshInspector()
            PANEL_ASSETS -> refreshAssets()
            PANEL_SCRIPTS -> refreshScripts()
            PANEL_ANIM -> refreshAnim()
        }
    }

    private fun buildHierarchyPanel() {
        val bar = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        bar.addView(
            toolButton("+ Entidade") { createEntityDialog() },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        bar.addView(
            toolButton("+ Sprite") { addSpriteDialog() },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        hierarchyList = ListView(this).apply {
            adapter = hierarchyAdapter
            onItemClickListener =
                AdapterView.OnItemClickListener { _, _, _, _ ->
                    val packed = hierarchyAdapter.selectedAt()
                    if (packed != 0L) {
                        // P1.0 BUG FIX: a seleção via hierarquia precisa
                        // chegar ao DOCUMENTO (borda no viewport + alvo do
                        // gizmo) — antes só o Kotlin sabia.
                        if (!EditorJni.nativeEditorSelect(handle, packed)) {
                            toast(lastErrorText())
                        }
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
    private lateinit var assetSearch: EditText
    private var assetQuery: String = ""

    // --- painel de ANIMAÇÃO (P2 §8) ----------------------------------------------

    private lateinit var animList: ListView
    private val animNames = mutableListOf<String>()

    private fun buildAnimPanel() {
        val bar = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        bar.addView(
            toolButton("+ Nova animação") { newAnimDialog() },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        bar.addView(
            toolButton("▶ Preview") { toggleAnimPreview() },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        animList = ListView(this).apply {
            onItemClickListener =
                AdapterView.OnItemClickListener { _, _, position, _ ->
                    animNames.getOrNull(position)?.let { animMenuDialog(it) }
                }
        }
        panelContainer.addView(
            bar,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
            )
        )
        panelContainer.addView(
            animList,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
    }

    private fun refreshAnim() {
        if (handle == 0L || !::animList.isInitialized) return
        val tsv = EditorJni.nativeEditorAnimationList(handle) ?: return
        animNames.clear()
        val display = mutableListOf<String>()
        for (line in tsv.lines().filter { it.isNotBlank() }) {
            val p = line.split('\t')
            animNames.add(p.getOrNull(0) ?: continue)
            // name      clip    duration        frames          keys    loop
            val clip = p.getOrNull(1) ?: "?"
            val dur = p.getOrNull(2)?.toFloatOrNull() ?: 0f
            val frames = p.getOrNull(3)?.toIntOrNull() ?: 0
            val keys = p.getOrNull(4)?.toIntOrNull() ?: 0
            val loop = p.getOrNull(5) == "1"
            display.add(
                "$clip  ${"%.1f".format(dur)}s  ${frames}f ${keys}k" +
                    (if (loop) " ∞" else "")
            )
        }
        // Linha única: "clip 1.0s 4f 2k ∞" (nome do arquivo no título).
        animList.adapter = ArrayAdapter(
            this, android.R.layout.simple_list_item_1, display
        )
    }

    private fun newAnimDialog() {
        inputDialog("Nome da animação", "walk") { name ->
            if (EditorJni.nativeEditorAnimationCreate(handle, name)) {
                toast("Animação criada — adicione frames com texturas reais")
                refreshAnim()
            } else {
                toast(lastErrorText())
            }
        }
    }

    /** Menu de contexto da animação: adicionar frame, fps/loop, anexar,
     * preview, editar JSON, apagar. */
    private fun animMenuDialog(name: String) {
        val items = arrayOf(
            "Adicionar frame (textura)…", "FPS / Loop…", "Anexar à seleção…",
            "Preview na seleção", "Editar JSON…", "Apagar"
        )
        AlertDialog.Builder(this)
            .setTitle(name)
            .setItems(items) { _, which ->
                when (which) {
                    0 -> pickFrameTexture(name)
                    1 -> animMetaDialog(name)
                    2 -> {
                        if (selection == 0L) {
                            toast("Selecione uma entidade primeiro")
                        } else if (EditorJni.nativeEditorAnimationAssign(
                                handle, selection, name
                            )
                        ) {
                            toast("Animação anexada — Play executa o clip real")
                            refreshInspectorIfOpen()
                        } else {
                            toast(lastErrorText())
                        }
                    }
                    3 -> {
                        if (selection == 0L) {
                            toast("Selecione uma entidade primeiro")
                        } else if (EditorJni.nativeEditorPreviewStart(
                                handle, selection, name
                            )
                        ) {
                            toast("Preview RODANDO — o transform original volta no Stop")
                        } else {
                            toast(lastErrorText())
                        }
                    }
                    4 -> animJsonEditorDialog(name)
                    5 -> if (EditorJni.nativeEditorAnimationDelete(handle, name)) {
                        refreshAnim()
                    } else {
                        toast(lastErrorText())
                    }
                }
            }
            .show()
    }

    /** Frame = textura REAL do projeto (picker com thumbnails — §8). */
    private fun pickFrameTexture(animName: String) {
        val tsv = EditorJni.nativeEditorListTextures(handle) ?: return
        val names = tsv.lines().filter { it.isNotBlank() }
        if (names.isEmpty()) {
            toast("Nenhuma textura importada (Assets → textures → Importar)")
            return
        }
        AlertDialog.Builder(this)
            .setTitle("Frame: textura")
            .setItems(names.toTypedArray()) { _, which ->
                val when_ = EditorJni.nativeEditorAnimationAddFrame(
                    handle, animName, names[which]
                )
                if (when_ >= 0f) {
                    toast("Frame em t=${"%.2f".format(when_)}s")
                    refreshAnim()
                } else {
                    toast(lastErrorText())
                }
            }
            .show()
    }

    private fun animMetaDialog(name: String) {
        val input = EditText(this).apply {
            hint = "FPS (1..120)"
            setSingleLine()
            inputType = InputType.TYPE_CLASS_NUMBER
            setPadding(dp(16), dp(10), dp(16), dp(10))
        }
        val loopCheck = android.widget.CheckBox(this).apply {
            text = "Loop"
            setPadding(dp(16), dp(4), dp(16), dp(10))
        }
        val box = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(input); addView(loopCheck)
        }
        AlertDialog.Builder(this)
            .setTitle("FPS / Loop")
            .setView(box)
            .setPositiveButton("OK") { _, _ ->
                val fps = input.text.toString().toFloatOrNull() ?: 8f
                if (!EditorJni.nativeEditorAnimationSetMeta(
                        handle, name, loopCheck.isChecked, fps
                    )
                ) {
                    toast(lastErrorText())
                }
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    /** Editor JSON cru (round-trip validado no C++ — lixo é rejeitado). */
    private fun animJsonEditorDialog(name: String) {
        val content = EditorJni.nativeEditorAnimationRead(handle, name) ?: run {
            toast(lastErrorText()); return
        }
        val edit = EditText(this).apply {
            setText(content)
            setTypeface(android.graphics.Typeface.MONOSPACE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            minLines = 12
            gravity = Gravity.TOP
        }
        val scroll = ScrollView(this).apply { addView(edit) }
        AlertDialog.Builder(this)
            .setTitle(name)
            .setView(scroll)
            .setPositiveButton("Salvar") { _, _ ->
                if (!EditorJni.nativeEditorAnimationWrite(handle, name, edit.text.toString())) {
                    toast(lastErrorText())
                }
            }
            .setNeutralButton("Cancelar", null)
            .show()
    }

    /** Preview liga/desliga na SELEÇÃO (o clip precisa estar anexado ou é
     * escolhido pelo nome do painel). */
    private fun toggleAnimPreview() {
        if (EditorJni.nativeEditorPreviewing(handle)) {
            EditorJni.nativeEditorPreviewStop(handle)
            toast("Preview parado — transform restaurado")
            return
        }
        if (selection == 0L) {
            toast("Selecione uma entidade e anexe uma animação primeiro")
            return
        }
        // Pega o clip do Animator da seleção (Inspector path canônico).
        val fields = EditorJni.nativeEditorComponentFields(
            handle, selection, "eng::animation::Animator"
        ) ?: run { toast("Entidade sem Animator — anexe no painel Animação"); return }
        val clip = fields.lines().firstOrNull { it.startsWith("clip\t") }
            ?.split('\t')?.getOrNull(2)
        if (clip.isNullOrBlank()) {
            toast("Animator sem clip")
            return
        }
        if (EditorJni.nativeEditorPreviewStart(handle, selection, clip)) {
            toast("Preview RODANDO (clip '$clip')")
        } else {
            toast(lastErrorText())
        }
    }

    // --- painel de scripts NI-Script (evolução P0-7, ADR-053) -------------------

    private fun buildScriptsPanel() {
        val bar = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        bar.addView(
            toolButton("+ Novo script") { newScriptDialog() },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        scriptsList = ListView(this).apply {
            onItemClickListener =
                AdapterView.OnItemClickListener { _, _, position, _ ->
                    val name = scriptNames.getOrNull(position)
                    if (name != null) scriptEditorDialog(name)
                }
        }
        panelContainer.addView(
            bar,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
            )
        )
        panelContainer.addView(
            scriptsList,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
    }

    private fun refreshScripts() {
        if (handle == 0L || !::scriptsList.isInitialized) return
        val tsv = EditorJni.nativeEditorScriptList(handle) ?: return
        scriptNames.clear()
        scriptNames.addAll(tsv.lines().filter { it.isNotBlank() })
        scriptsList.adapter = ArrayAdapter(
            this, android.R.layout.simple_list_item_1, scriptNames.toList()
        )
    }

    private fun newScriptDialog() {
        inputDialog("Nome do script", "Movimento") { name ->
            if (EditorJni.nativeEditorScriptCreate(handle, name)) {
                refreshScripts()
            } else {
                toast(lastErrorText())
            }
        }
    }

    /**
     * Editor de script (P0-7): fonte multi-linha (monospace), Compilar
     * com diagnósticos line:col, Anexar à entidade selecionada, Salvar.
     */
    private fun scriptEditorDialog(name: String) {
        val content = EditorJni.nativeEditorScriptRead(handle, name) ?: run {
            toast(lastErrorText()); return
        }

        val container = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(12), dp(4), dp(12), dp(4))
        }
        val edit = EditText(this).apply {
            setText(content)
            setTypeface(android.graphics.Typeface.MONOSPACE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            setTextColor(Ui.TEXT)
            setHorizontallyScrolling(false)
            inputType = InputType.TYPE_CLASS_TEXT or
                InputType.TYPE_TEXT_FLAG_MULTI_LINE or
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            gravity = Gravity.TOP
            minLines = 12
            background = rippleBox(Ui.SURFACE_ALT, dp(6))
            setPadding(dp(8), dp(8), dp(8), dp(8))
        }
        val scroller = ScrollView(this).apply { addView(edit) }
        container.addView(
            scroller,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f
            )
        )

        // Barra de ações: Compilar | Anexar | Salvar.
        val actions = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, dp(6), 0, 0)
        }
        actions.addView(
            toolButton("Compilar") {
                val tsv = EditorJni.nativeEditorScriptCompile(handle, edit.text.toString())
                if (tsv == null) {
                    toast(lastErrorText())
                } else {
                    showCompileDiags(tsv)
                }
            },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        actions.addView(
            toolButton("Anexar") {
                if (selection == 0L) {
                    toast("Selecione uma entidade antes de anexar")
                } else if (EditorJni.nativeEditorScriptAssign(handle, selection, name)) {
                    toast("Anexado a ${currentEntityName(selection)}")
                } else {
                    toast(lastErrorText())
                }
            },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        var dialog: AlertDialog? = null
        actions.addView(
            toolButton("Salvar") {
                if (EditorJni.nativeEditorScriptWrite(handle, name, edit.text.toString())) {
                    toast("Salvo")
                    dialog?.dismiss()
                } else {
                    toast(lastErrorText())
                }
            },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        container.addView(
            actions,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
            )
        )

        dialog = AlertDialog.Builder(this)
            .setTitle(name)
            .setView(container)
            .setPositiveButton("Fechar", null)
            .show()
    }

    /** Diagnósticos do Compilar: TSV "1|0" + linhas "line\tcol\tmessage". */
    private fun showCompileDiags(tsv: String) {
        val lines = tsv.lines()
        val ok = lines.firstOrNull() == "1"
        val builder = AlertDialog.Builder(this)
            .setTitle(if (ok) "Compilou" else "Erros de compilação")
        if (ok) {
            builder.setMessage("O script compila até bytecode.")
        } else {
            val rows = lines.drop(1).filter { it.isNotBlank() }
            val list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
            val scroll = ScrollView(this).apply { addView(list) }
            for (row in rows) {
                val p = row.split('\t')
                list.addView(TextView(this).apply {
                    text = "${p.getOrNull(0) ?: "?"}:${p.getOrNull(1) ?: "?"}  ${p.getOrNull(2) ?: ""}"
                    setTextColor(Ui.DANGER)
                    setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
                    setTypeface(android.graphics.Typeface.MONOSPACE)
                    setPadding(dp(12), dp(4), dp(12), dp(4))
                })
            }
            builder.setView(scroll)
        }
        builder.setPositiveButton("OK", null).show()
    }

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
            LinearLayout.LayoutParams(0, dp(36), 1.4f)
        )
        importButton = toolButton("Importar") { onImportButton() }
        bar.addView(
            importButton,
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        // Busca de assets (P0-6): filtra por nome dentro da categoria.
        assetSearch = EditText(this).apply {
            hint = "Buscar…"
            setSingleLine()
            textSize = 13f
            setTextColor(Ui.TEXT)
            setPadding(dp(12), dp(8), dp(12), dp(8))
            background = rippleBox(Ui.SURFACE_ALT, dp(6))
            addTextChangedListener(object : android.text.TextWatcher {
                override fun afterTextChanged(s: android.text.Editable?) {
                    assetQuery = s?.toString() ?: ""
                    refreshAssets()
                }
                override fun beforeTextChanged(
                    s: CharSequence?, a: Int, b: Int, c: Int
                ) {}
                override fun onTextChanged(
                    s: CharSequence?, a: Int, b: Int, c: Int
                ) {}
            })
        }
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
            assetSearch,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(40)
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

        // Transform (TRS com Euler em graus — API do documento). Os campos
        // ficam REFERENCIADOS (transformFields) para o live sync do doFrame
        // (P1.9: gizmo → Inspector ao vivo, sem rebuild do painel).
        transformFields.clear()
        collectTransformFields = true
        val tr = EditorJni.nativeEditorGetTransform(handle, selection)
        if (tr != null && tr.size == 9) {
            content.addView(sectionTitle("Transform"))
            // Aplica lendo TODOS os 9 campos vivos (não o snapshot `tr`):
            // o gizmo pode ter mexido rotação/escala DEPOIS do build do
            // painel — usar `tr` STALE reverteria a edição (P1.9).
            addVec3Row(content, "Posição", tr[0], tr[1], tr[2]) { _ ->
                applyTransformFromFields()
            }
            addVec3Row(content, "Rotação (°)", tr[3], tr[4], tr[5]) { _ ->
                applyTransformFromFields()
            }
            addVec3Row(content, "Escala", tr[6], tr[7], tr[8]) { _ ->
                applyTransformFromFields()
            }
        }
        collectTransformFields = false

        // Componentes (catálogo reflect-driven — §8.4; kinds P0-6/ADR-052).
        val componentsTsv = EditorJni.nativeEditorEntityComponents(handle, selection)
        if (componentsTsv != null) {
            for (line in componentsTsv.lines().filter { it.isNotBlank() }) {
                val parts = line.split('\t')
                if (parts.size < 2) continue
                val component = parts[0]
                val removable = parts[1] == "1"
                content.addView(sectionTitle(prettyComponent(component)))
                val fieldsTsv =
                    EditorJni.nativeEditorComponentFields(handle, selection, component)
                if (fieldsTsv != null) {
                    for (fline in fieldsTsv.lines().filter { it.isNotBlank() }) {
                        val fp = fline.split('\t')
                        if (fp.size < 3) continue
                        addFieldRow(
                            content, component, fp[0], fp[1], fp[2],
                            fp.getOrElse(3) { "text" }, fp.getOrElse(4) { "" }
                        )
                    }
                }
                if (removable) {
                    content.addView(
                        toolButton("Remover ${prettyComponent(component)}") {
                            val ok = EditorJni.nativeEditorRemoveComponent(handle, selection, component)
                            if (!ok) toast(lastErrorText())
                            refreshPanel()
                        },
                        LinearLayout.LayoutParams(
                            ViewGroup.LayoutParams.MATCH_PARENT, dp(36)
                        )
                    )
                }
            }
        }
        content.addView(
            toolButton("+ Adicionar componente") { addComponentDialog() },
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(36))
        )

        // Ações rápidas da seleção (P1.7/P1.8 — um toque, sem long-press).
        val quick = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        quick.addView(
            toolButton("Duplicar") {
                val dup = EditorJni.nativeEditorDuplicateEntity(handle, selection)
                if (dup == 0L) toast(lastErrorText()) else selectEntity(dup)
            },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        quick.addView(
            toolButton("Apagar") {
                if (EditorJni.nativeEditorDeleteEntity(handle, selection)) {
                    selection = 0L
                    refreshPanel()
                } else {
                    toast(lastErrorText())
                }
            },
            LinearLayout.LayoutParams(0, dp(36), 1f)
        )
        content.addView(quick)

        inspectorScroll.removeAllViews()
        inspectorScroll.addView(content)
    }

    /** Escreve o transform com os 9 CAMPOS vivos do painel (P1.9). */
    private fun applyTransformFromFields() {
        if (transformFields.size != 9) return
        val values = FloatArray(9) { i ->
            transformFields[i].text.toString().toFloatOrNull() ?: 0f
        }
        // Escala inválida (0/negativa/NaN → 0 por fallback) é recusada no
        // campo numérico; o documento mantém a última válida.
        if (!EditorJni.nativeEditorSetTransform(
                handle, selection,
                values[0], values[1], values[2],
                values[3], values[4], values[5],
                values[6], values[7], values[8]
            )
        ) {
            toast(lastErrorText())
        }
        updateTransformFieldsLive()  // ecoa o que o documento aceitou
    }

    /** Rebuild do Inspector quando aberto (drag de gizmo terminou — P1.9). */
    private fun refreshInspectorIfOpen() {
        if (activePanel == PANEL_INSPECTOR && selection != 0L) {
            refreshInspector()
        }
    }

    /**
     * Live sync (P1.9): atualiza SO os campos de transform do painel
     * aberto — sem rebuild. Pula campos com FOCO (o usuário está
     * digitando; o teclado é a fonte daquele campo até o DONE).
     */
    private fun updateTransformFieldsLive() {
        if (activePanel != PANEL_INSPECTOR || selection == 0L) return
        if (transformFields.size != 9) return
        val tr = EditorJni.nativeEditorGetTransform(handle, selection)
            ?: return
        if (tr.size != 9) return
        for (i in 0 until 9) {
            val field = transformFields[i]
            if (field.hasFocus()) continue  // digitando: não pisca
            field.setText(fmtFloat(tr[i]))
        }
    }

    /** Importar vs "Novo material…" (P3 §3): materiais são AUTORADOS,
     *  não importados — o botão da categoria reflete isso. */
    private fun onImportButton() {
        val category = assetCategory.selectedItem?.toString() ?: ""
        if (category == "materials") {
            inputDialog("Nome do material", "NovoMaterial") { name ->
                if (EditorJni.nativeEditorMaterialCreate(handle, name)) {
                    toast("Material '$name' criado")
                    refreshAssets()
                } else toast(lastErrorText())
            }
            return
        }
        pickImportFile()
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
        if (::importButton.isInitialized) {
            importButton.text = if (category == "materials") "Novo…" else "Importar"
        }
        val tsv = EditorJni.nativeEditorAssetList(handle, category)
        // Busca (P0-6): filtro por nome, insensível a caixa.
        val filtered = (tsv ?: "").lines().filter {
            it.isNotBlank() && (assetQuery.isBlank() ||
                it.contains(assetQuery, ignoreCase = true))
        }
        assetAdapter.reload(filtered)
        assetAdapter.notifyDataSetChanged()
    }

    private fun refreshAll() {
        refreshHierarchySafe()
        if (::btnProject.isInitialized && handle != 0L) {
            val name = EditorJni.nativeEditorProjectName(handle) ?: ""
            // Marca carrega o projeto: identidade + contexto na MESMA linha
            // (evolução P0-4 — sem botão gigante de projeto).
            brand.text = if (name.isEmpty()) "G.ONI" else "G.ONI · $name"
        }
    }

    private fun refreshHierarchySafe() {
        if (::hierarchyAdapter.isInitialized && activePanel == PANEL_HIERARCHY) {
            refreshHierarchy()
        }
    }

    // --- helpers de UI ----------------------------------------------------------

    /** Nome de exibição de componente: "eng::editor::SpriteData" → "Sprite"
     * (P0-6: cabeçalhos legíveis; a CHAMADA de API continua com o nome cru). */
    private fun prettyComponent(raw: String): String {
        var name = raw.substringAfterLast(':')
        if (name.endsWith("Data")) name = name.removeSuffix("Data")
        if (name.endsWith("Component")) name = name.removeSuffix("Component")
        return name
    }

    /** Rótulo de campo: último segmento do path; grupo de cor → rótulo base. */
    private fun prettyFieldLabel(path: String): String {
        val leaf = path.substringBefore(',').substringAfterLast('.')
        return leaf.replaceFirstChar { it.uppercase() }
    }

    /** Hex "#RRGGBB[AA]" → ARGB int (ou null quando inválido). */
    private fun parseHexColor(hex: String): Int? {
        if (!hex.startsWith("#")) return null
        val digits = hex.substring(1)
        if (digits.length != 6 && digits.length != 8) return null
        val value = digits.toIntOrNull(16) ?: return null
        return if (digits.length == 6) {
            0xFF000000.toInt() or value
        } else {
            value
        }
    }

    /** Escreve o valor de um campo com feedback (toast em erro). */
    private fun setFieldQuiet(component: String, path: String, value: String) {
        val ok = EditorJni.nativeEditorSetComponentField(
            handle, selection, component, path, value
        )
        if (!ok) toast(lastErrorText())
    }

    private fun labelView(text: String): TextView =
        TextView(this).apply {
            this.text = text
            setTextColor(Ui.TEXT_DIM)
            setPadding(dp(8), dp(8), dp(8), dp(4))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
        }

    private fun sectionTitle(text: String): TextView =
        TextView(this).apply {
            this.text = text
            setTextColor(Ui.ACCENT)
            setPadding(dp(4), dp(10), dp(4), dp(2))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            typeface = android.graphics.Typeface.DEFAULT_BOLD
            setBackgroundColor(Ui.SURFACE_ALT)
        }

    private fun addVec3Row(
        parent: LinearLayout, title: String, x: Float, y: Float, z: Float,
        apply: (FloatArray) -> Unit
    ) {
        parent.addView(labelView(title))
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val fields = mutableListOf<EditText>()
        if (collectTransformFields) transformFields.addAll(fields)
        for (value in listOf(x, y, z)) {
            val edit = EditText(this).apply {
                inputType = InputType.TYPE_CLASS_NUMBER or
                    InputType.TYPE_NUMBER_FLAG_SIGNED or
                    InputType.TYPE_NUMBER_FLAG_DECIMAL
                setSingleLine()
                setText(fmtFloat(value))
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
                setTextColor(Ui.TEXT)
                setPadding(dp(6), dp(8), dp(6), dp(8))
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
                LinearLayout.LayoutParams(0, dp(40), 1f)
            )
        }
        parent.addView(row)
    }

    /**
     * Linha de campo do Inspector (P0-6, ADR-052): o kind semântico vindo do
     * C++ decide o editor — Switch (bool), dropdown (enum), swatch+sliders
     * (color), picker de textura, campo numérico ou texto. Nada de digitar
     * "true"/"Sphere"/hex à mão.
     */
    private fun addFieldRow(
        parent: LinearLayout, component: String, path: String,
        typeName: String, value: String, kind: String, options: String
    ) {
        when (kind) {
            "bool" -> {
                val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
                row.addView(
                    labelView(prettyFieldLabel(path)),
                    LinearLayout.LayoutParams(0, dp(44), 1f)
                )
                val sw = Switch(this).apply {
                    isChecked = value == "true"
                    setTextColor(Ui.TEXT)
                    setOnCheckedChangeListener { _, checked ->
                        setFieldQuiet(component, path, if (checked) "true" else "false")
                    }
                }
                row.addView(sw, LinearLayout.LayoutParams(dp(84), dp(44)))
                parent.addView(row)
                return
            }
            "enum" -> {
                val choices = options.split('|').filter { it.isNotEmpty() }
                val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
                row.addView(
                    labelView(prettyFieldLabel(path)),
                    LinearLayout.LayoutParams(0, dp(44), 0.9f)
                )
                val current = TextView(this).apply {
                    text = value
                    setTextColor(Ui.ACCENT)
                    setPadding(dp(8), dp(12), dp(8), dp(12))
                    setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                    setOnClickListener {
                        AlertDialog.Builder(this@EditorActivity)
                            .setTitle(prettyFieldLabel(path))
                            .setSingleChoiceItems(
                                choices.toTypedArray(),
                                choices.indexOf(value)
                            ) { dialog, which ->
                                setFieldQuiet(component, path, choices[which])
                                dialog.dismiss()
                                refreshInspector()
                            }
                            .setNegativeButton("Cancelar", null)
                            .show()
                    }
                }
                row.addView(current, LinearLayout.LayoutParams(0, dp(44), 1.1f))
                parent.addView(row)
                return
            }
            "color" -> {
                val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
                row.addView(
                    labelView(prettyFieldLabel(path)),
                    LinearLayout.LayoutParams(0, dp(44), 0.9f)
                )
                val initial = parseHexColor(value) ?: 0xFFFFFFFF.toInt()
                val swatch = TextView(this).apply {
                    text = value
                    setTextColor(Ui.TEXT)
                    textSize = 12f
                    gravity = Gravity.CENTER_VERTICAL or Gravity.END
                    setPadding(dp(12), dp(6), dp(12), dp(6))
                    background = rippleBox(initial and 0xFFFFFF or 0xFF000000.toInt(), dp(6))
                    setOnClickListener {
                        colorPickerDialog(component, path, value, initial) { hex, argb ->
                            background = rippleBox(argb, dp(6))
                            text = hex
                        }
                    }
                }
                row.addView(swatch, LinearLayout.LayoutParams(0, dp(44), 1.1f))
                parent.addView(row)
                return
            }
            "texture" -> {
                parent.addView(labelView(prettyFieldLabel(path)))
                val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
                val current = TextView(this).apply {
                    text = value.ifEmpty { "(nenhuma)" }
                    setTextColor(0xFF8AB4F8.toInt())
                    setPadding(dp(8), dp(12), dp(8), dp(12))
                    setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                }
                row.addView(current, LinearLayout.LayoutParams(0, dp(44), 1f))
                row.addView(
                    Button(this).apply {
                        text = "Escolher…"
                        minHeight = 0
                        setPadding(dp(10), 0, dp(10), 0)
                        height = dp(36)
                        setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                        isAllCaps = false
                        setTextColor(Ui.TEXT)
                        background = rippleBox(Ui.SURFACE_ALT, dp(6))
                        setOnClickListener {
                            pickTextureFor(component, path) { chosen ->
                                current.text = chosen.ifEmpty { "(nenhuma)" }
                            }
                        }
                    },
                    LinearLayout.LayoutParams(0, dp(36), 0.8f)
                )
                parent.addView(row)
                return
            }
            "audio" -> {
                // P2 (§12): AudioSource.soundAsset — picker de WAVs do projeto
                // + preview que toca AGORA (mesma via do Play: mixer real).
                parent.addView(labelView(prettyFieldLabel(path)))
                val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
                val current = TextView(this).apply {
                    text = value.ifEmpty { "(nenhum)" }
                    setTextColor(0xFF8AB4F8.toInt())
                    setPadding(dp(8), dp(12), dp(8), dp(12))
                    setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                }
                row.addView(current, LinearLayout.LayoutParams(0, dp(44), 1f))
                row.addView(
                    Button(this).apply {
                        text = "Ouvir"
                        minHeight = 0
                        setPadding(dp(10), 0, dp(10), 0)
                        height = dp(36)
                        setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                        isAllCaps = false
                        setTextColor(Ui.TEXT)
                        background = rippleBox(Ui.SURFACE_ALT, dp(6))
                        setOnClickListener {
                            if (value.isNotBlank()) {
                                if (!EditorJni.nativeEditorAudioPreview(handle, value)) {
                                    toast(lastErrorText())
                                }
                            }
                        }
                    },
                    LinearLayout.LayoutParams(0, dp(36), 0.6f)
                )
                row.addView(
                    Button(this).apply {
                        text = "Escolher…"
                        minHeight = 0
                        setPadding(dp(10), 0, dp(10), 0)
                        height = dp(36)
                        setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                        isAllCaps = false
                        setTextColor(Ui.TEXT)
                        background = rippleBox(Ui.SURFACE_ALT, dp(6))
                        setOnClickListener {
                            pickAudioFor(component, path) { chosen ->
                                current.text = chosen.ifEmpty { "(nenhum)" }
                            }
                        }
                    },
                    LinearLayout.LayoutParams(0, dp(36), 0.8f)
                )
                parent.addView(row)
                return
            }
            "material" -> {
                // P3 §3: SpriteData.materialAsset — picker de materiais do
                // projeto (vazio = default lit neutro).
                parent.addView(labelView(prettyFieldLabel(path)))
                val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
                val current = TextView(this).apply {
                    text = value.ifEmpty { "(default lit)" }
                    setTextColor(0xFF8AB4F8.toInt())
                    setPadding(dp(8), dp(12), dp(8), dp(12))
                    setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                }
                row.addView(current, LinearLayout.LayoutParams(0, dp(44), 1f))
                row.addView(
                    Button(this).apply {
                        text = "Escolher…"
                        minHeight = 0
                        setPadding(dp(10), 0, dp(10), 0)
                        height = dp(36)
                        setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                        isAllCaps = false
                        setTextColor(Ui.TEXT)
                        background = rippleBox(Ui.SURFACE_ALT, dp(6))
                        setOnClickListener {
                            pickMaterialFor(component, path) { chosen ->
                                current.text = chosen.ifEmpty { "(default lit)" }
                            }
                        }
                    },
                    LinearLayout.LayoutParams(0, dp(36), 0.8f)
                )
                parent.addView(row)
                return
            }
        }

        // number/int/text → EditText (numérico quando aplicável).
        val numeric = kind == "number" || kind == "int"
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        row.addView(
            labelView(prettyFieldLabel(path)),
            LinearLayout.LayoutParams(0, dp(44), 0.7f)
        )
        val edit = EditText(this).apply {
            setSingleLine()
            inputType = if (numeric) {
                InputType.TYPE_CLASS_NUMBER or
                    InputType.TYPE_NUMBER_FLAG_SIGNED or
                    InputType.TYPE_NUMBER_FLAG_DECIMAL
            } else {
                InputType.TYPE_CLASS_TEXT
            }
            setText(value)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            setTextColor(Ui.TEXT)
            setPadding(dp(6), dp(6), dp(6), dp(6))
            imeOptions = android.view.inputmethod.EditorInfo.IME_ACTION_DONE
            setOnEditorActionListener { _, actionId, _ ->
                if (actionId == android.view.inputmethod.EditorInfo.IME_ACTION_DONE) {
                    setFieldQuiet(component, path, text.toString())
                    true
                } else false
            }
        }
        row.addView(
            edit,
            LinearLayout.LayoutParams(0, dp(44), 1.3f)
        )
        parent.addView(row)
    }

    /**
     * Editor de cor REAL (P0-6): sliders R/G/B (+A quando o grupo tem 4
     * canais) com preview ao vivo + hex — nativo, sem dependências.
     */
    private fun colorPickerDialog(
        component: String, path: String, initialHex: String, initialArgb: Int,
        onApplied: (String, Int) -> Unit
    ) {
        val hasAlpha = initialHex.length == 9
        var r = (initialArgb shr 16) and 0xFF
        var g = (initialArgb shr 8) and 0xFF
        var b = initialArgb and 0xFF
        var a = (initialArgb shr 24) and 0xFF

        val container = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(8), dp(16), dp(4))
        }
        val preview = TextView(this).apply {
            text = initialHex
            setTextColor(Ui.TEXT)
            gravity = Gravity.CENTER
            textSize = 14f
            height = dp(56)
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
                    setTextColor(Ui.TEXT_DIM)
                    width = dp(28)
                },
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT
                )
            )
            row.addView(
                SeekBar(this).apply {
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
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
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

        AlertDialog.Builder(this)
            .setTitle(prettyFieldLabel(path))
            .setView(container)
            .setPositiveButton("OK") { _, _ ->
                val hex = currentHex()
                setFieldQuiet(component, path, hex)
                onApplied(hex, currentArgb())
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    /** kind="material" — picker dos assets/materials (P3 §3). */
    private fun pickMaterialFor(
        component: String, path: String, onApplied: (String) -> Unit
    ) {
        val tsv = EditorJni.nativeEditorListMaterials(handle)
        val names = (tsv ?: "").lines().filter { it.isNotBlank() }
        val options = mutableListOf<String>()
        options.add("")  // (default lit neutro)
        options.addAll(names)
        val labels = options.map { it.ifEmpty { "(default lit)" } }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle("Material do sprite")
            .setItems(labels) { _, which ->
                setFieldQuiet(component, path, options[which])
                onApplied(options[which])
            }
            .setNeutralButton("Novo…") { _, _ ->
                inputDialog("Nome do material", "NovoMaterial") { name ->
                    if (EditorJni.nativeEditorMaterialCreate(handle, name)) {
                        toast("Material '$name' criado (edite em Assets)")
                        setFieldQuiet(component, path, "${name}.mat.json")
                        onApplied("${name}.mat.json")
                    } else toast(lastErrorText())
                }
            }
            .show()
    }

    /** Edição de material (Assets → materials, long-press): shader + cor.
     *  Escreve via materialWrite (valida no codec — lixo não entra). */
    private fun editMaterialDialog(name: String) {
        val json = EditorJni.nativeEditorMaterialRead(handle, name)
        if (json == null) {
            toast(lastErrorText())
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
            setPadding(dp(16), dp(12), dp(16), dp(8))
        }
        val shaderLabel = TextView(this).apply {
            text = "Shader: ${if (shader == "unlit") "unlit" else "lit"}"
            setTextColor(Ui.TEXT)
            setPadding(0, dp(4), 0, dp(8))
        }
        val alphaEdit = EditText(this).apply {
            setSingleLine()
            inputType = android.text.InputType.TYPE_CLASS_NUMBER or
                android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL
            setText(String.format("%.2f", tintA))
            hint = "Alfa do tint (0-1)"
        }
        layout.addView(shaderLabel)
        layout.addView(alphaEdit)
        AlertDialog.Builder(this)
            .setTitle("Material $name")
            .setView(layout)
            .setPositiveButton("Salvar") { _, _ ->
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
                    toast("Material salvo")
                    refreshAssets()
                } else toast(lastErrorText())
            }
            .setNeutralButton("Shader") { _, _ ->
                // Alterna lit/unlit e REABRE o diálogo (fluxo simples).
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
                } else toast(lastErrorText())
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    /**
     * Picker de textura COM THUMBNAILS (P0-6): lista os assets de textura do
     * projeto com preview real e atribui no campo genérico (qualquer campo
     * kind="texture" — não apenas SpriteData.textureAsset).
     */
    private fun pickTextureFor(
        component: String, path: String, onApplied: (String) -> Unit
    ) {
        val tsv = EditorJni.nativeEditorListTextures(handle) ?: return
        val names = tsv.lines().filter { it.isNotBlank() }
        if (names.isEmpty()) {
            toast("Nenhuma textura importada (Assets → textures → Importar)")
            return
        }
        val list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val scroll = ScrollView(this).apply { addView(list) }
        var picker: AlertDialog? = null
        for (name in names) {
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(dp(12), dp(6), dp(12), dp(6))
                background = rippleBox(Ui.SURFACE_ALT, dp(6))
                setOnClickListener {
                    setFieldQuiet(component, path, name)
                    onApplied(name)
                    picker?.dismiss()
                }
            }
            thumbnailOf("textures", name)?.let { bmp ->
                row.addView(
                    ImageView(this).apply {
                        setImageBitmap(bmp)
                        scaleType = android.widget.ImageView.ScaleType.FIT_CENTER
                        background = rippleBox(Ui.BORDER, dp(4))
                        clipToOutline = true
                    },
                    LinearLayout.LayoutParams(dp(44), dp(44))
                )
                row.addView(
                    Space(this),
                    LinearLayout.LayoutParams(dp(10), dp(1))
                )
            }
            row.addView(
                TextView(this).apply {
                    text = name
                    setTextColor(Ui.TEXT)
                    setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                },
                LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
            )
            list.addView(
                row,
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
                )
            )
        }
        picker = AlertDialog.Builder(this)
            .setTitle("Textura")
            .setView(scroll)
            .setNegativeButton("Cancelar", null)
            .show()
    }

    /** Picker de áudio (P2 §12): WAVs de assets/audio — mesma forma do
     * picker de texturas (sem thumbnail: áudio não é imagem). */
    private fun pickAudioFor(
        component: String, path: String, onApplied: (String) -> Unit
    ) {
        val tsv = EditorJni.nativeEditorListAudio(handle) ?: return
        val names = tsv.lines().filter { it.isNotBlank() }
        if (names.isEmpty()) {
            toast("Nenhum áudio importado (Assets → audio → Importar)")
            return
        }
        AlertDialog.Builder(this)
            .setTitle("Áudio")
            .setItems(names.toTypedArray()) { _, which ->
                setFieldQuiet(component, path, names[which])
                onApplied(names[which])
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    // --- thumbnails (P0-6): decode com inSampleSize + cache em memória ---------

    private val thumbCache = HashMap<String, android.graphics.Bitmap>()

    /** Bitmap reduzido do asset (textures) para linhas/pickers — null se não
     * é imagem decodificável. Cache por nome (chave: categoria/nome). */
    private fun thumbnailOf(category: String, name: String): android.graphics.Bitmap? {
        val key = "$category/$name"
        thumbCache[key]?.let { return it }
        val project = EditorJni.nativeEditorProjectName(handle) ?: return null
        val file = File(File(File(filesDir, "projects"), project),
                        "assets/$category/$name")
        if (!file.isFile) return null
        // 1ª passada: só dimensões.
        val bounds = android.graphics.BitmapFactory.Options().apply {
            inJustDecodeBounds = true
        }
        android.graphics.BitmapFactory.decodeFile(file.absolutePath, bounds)
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
        // inSampleSize: maior potência de 2 que ainda cabe em 96px.
        var sample = 1
        while (bounds.outWidth / (sample * 2) >= 96 &&
               bounds.outHeight / (sample * 2) >= 96) {
            sample *= 2
        }
        val opts = android.graphics.BitmapFactory.Options().apply {
            inSampleSize = sample
        }
        val bmp = android.graphics.BitmapFactory.decodeFile(file.absolutePath, opts)
            ?: return null
        thumbCache[key] = bmp
        return bmp
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

    /**
     * Startup do projeto (P3 §0 — bug Android "AlreadyExists").
     *
     * CAUSA RAIZ do bug: hasProject() (estado EM MEMÓRIA) era usado como
     * detector de "primeira execução" — num processo novo ele é SEMPRE
     * false, então toda reentrada chamava newProject("MeuJogo") sobre o
     * projeto que JÁ EXISTIA no disco (AlreadyExists + editor sem
     * projeto). A política correta (criar quando não há NENHUM projeto /
     * reabrir o último usado / default / primeiro) vive no C++ e é
     * testada no Linux; aqui só reportamos o erro controlado.
     */
    private fun ensureProjectOnFirstRun() {
        val opened = EditorJni.nativeEditorEnsureProject(handle)
        if (opened == null) {
            toast(lastErrorText())
        }
    }

    private fun showProjectMenu() {
        val items = arrayOf(
            "Novo projeto…", "Abrir projeto…", "Salvar projeto",
            "Configurações do projeto…",
            "Pasta de exportação (SAF)…", "Exportar projeto (zip)…",
            "Importar projeto (zip)…", "Diagnóstico (logcat)"
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
                    // P2 §17 — SAF: pasta de exportação com permissão
                    // PERSISTENTE (takePersistableUriPermission) + zip real
                    // de ida e volta. O projeto VIVE no workspace privado
                    // (sem permissões, zero risco); o SAF é o canal de
                    // intercâmbio com o armazenamento do usuário.
                    4 -> pickSafFolder()
                    5 -> exportProjectZip()
                    6 -> importProjectZip()
                    // P3 §0 — diagnóstico: estado completo no logcat
                    // [GONI] (operação/projeto/caminho/backend/frames). Se
                    // o app fechar de novo, `adb logcat -s GONI` mostra a
                    // ÚLTIMA operação viva antes da morte.
                    7 -> {
                        EditorJni.nativeEditorDumpState(handle, "menu-projeto")
                        toast("Estado gravado no logcat (tag GONI)")
                    }
                }
            }
            .show()
    }

    // --- SAF (P2 §17): armazenamento do usuário COM permissão persistente ------
    //
    // Modelo documentado: os PROJETOS vivem no workspace privado
    // (filesDir/projects — sem permissões, nunca "assume acesso
    // irrestrito", paths sempre relativos — RootedFileSystem na
    // fronteira). O SAF dá ao AUTOR o canal de intercâmbio:
    //   - Pasta de exportação: ACTION_OPEN_DOCUMENT_TREE +
    //     takePersistableUriPermission (sobrevive a reboots — o Android
    //     MANTÉM o grant; guardamos o URI em prefs, NÃO um path absoluto
    //     de arquivo);
    //   - Exportar: zip REAL do projeto escrito via ContentResolver;
    //   - Importar: zip lido via ContentResolver para o workspace.

    private val safPrefs by lazy {
        getSharedPreferences("goni_saf", Context.MODE_PRIVATE)
    }

    // Requests de SAF (dispatch no onActivityResult ÚNICO, abaixo).
    private val reqSafFolder = 4101
    private val reqSafExport = 4102
    private val reqSafImport = 4103

    /** Dispatch SAF (chamado pelo onActivityResult ÚNICO da Activity). */
    private fun handleSafResult(requestCode: Int, resultCode: Int, uri: Uri?) {
        if (resultCode != RESULT_OK || uri == null) return
        when (requestCode) {
            reqSafFolder -> {
                // Permissão PERSISTENTE: o grant sobrevive a restarts —
                // o mecanismo do Android (não um path absoluto salvocrado).
                try {
                    contentResolver.takePersistableUriPermission(
                        uri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION or
                            Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    )
                    safPrefs.edit().putString("export_tree", uri.toString()).apply()
                    toast("Pasta de exportação autorizada (permissão persistente)")
                } catch (e: SecurityException) {
                    toast("Sem permissão persistível: ${e.message}")
                }
            }
            reqSafExport -> writeProjectZipTo(uri)
            reqSafImport -> importProjectZipFrom(uri)
        }
    }

    private fun pickSafFolder() {
        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), reqSafFolder)
    }

    private fun exportProjectZip() {
        val project = EditorJni.nativeEditorProjectName(handle)
        if (project.isNullOrEmpty()) {
            toast("Nenhum projeto aberto")
            return
        }
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/zip"
            putExtra(Intent.EXTRA_TITLE, "$project.goni.zip")
        }
        startActivityForResult(intent, reqSafExport)
    }

    /** Zip REAL do projeto (assets + scenes + project.goni.json). */
    private fun writeProjectZipTo(uri: Uri) {
        val project = EditorJni.nativeEditorProjectName(handle) ?: return
        val root = File(File(filesDir, "projects"), project)
        if (!root.isDirectory) {
            toast("Pasta do projeto não encontrada")
            return
        }
        try {
            contentResolver.openOutputStream(uri)?.use { out ->
                ZipOutputStream(out).use { zip ->
                    root.walkTopDown().filter { it.isFile }.forEach { file ->
                        val entry =
                            ZipEntry(file.relativeTo(root).invariantSeparatorsPath)
                        zip.putNextEntry(entry)
                        file.inputStream().use { it.copyTo(zip) }
                        zip.closeEntry()
                    }
                }
            }
            toast("Projeto '$project' exportado")
        } catch (e: Exception) {
            toast("Export falhou: ${e.message}")
        }
    }

    private fun importProjectZip() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/zip"
            putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/zip", "application/octet-stream"))
        }
        startActivityForResult(intent, reqSafImport)
    }

    /** Importa o zip PARA O WORKSPACE (privado) e ABRE o projeto. */
    private fun importProjectZipFrom(uri: Uri) {
        try {
            // Nome do projeto: primeiro componente do zip (ou do nome do arquivo).
            var projectName: String? = null
            val staging = File(cacheDir, "saf_import").apply {
                deleteRecursively(); mkdirs()
            }
            contentResolver.openInputStream(uri)?.use { input ->
                ZipInputStream(input).use { zip ->
                    var entry: ZipEntry? = zip.nextEntry
                    while (entry != null) {
                        // Anti-traversal: caminhos com .. são rejeitados.
                        if (entry.name.contains("..")) {
                            toast("Entrada inválida no zip: ${entry.name}")
                            return
                        }
                        val out = File(staging, entry.name)
                        if (entry.isDirectory) {
                            out.mkdirs()
                        } else {
                            out.parentFile?.mkdirs()
                            FileOutputStream(out).use { zip.copyTo(it) }
                        }
                        val first = entry.name.substringBefore('/')
                        if (first.isNotEmpty()) projectName = first
                        zip.closeEntry()
                        entry = zip.nextEntry
                    }
                }
            }
            val name = projectName ?: run {
                toast("Zip sem estrutura de projeto")
                return
            }
            val target = File(File(filesDir, "projects"), name)
            if (target.exists()) {
                toast("Projeto '$name' já existe — renomeie o zip ou apague o atual")
                return
            }
            if (!staging.renameTo(target)) {
                toast("Falha ao mover o projeto para o workspace")
                return
            }
            if (EditorJni.nativeEditorOpenProject(handle, name)) {
                EditorJni.nativeEditorNewScene(handle)
                refreshAll()
                toast("Projeto '$name' importado e aberto")
            } else {
                toast(lastErrorText())
            }
        } catch (e: Exception) {
            toast("Import falhou: ${e.message}")
        }
    }

    private fun openProjectDialog() {
        val workspace = File(filesDir, "projects")
        // Diretórios ocultos (ex.: .import_tmp — staging do SAF) não são
        // projetos: o seletor lista apenas pastas reais de projeto.
        val projects = workspace.listFiles()
            ?.filter { it.isDirectory && !it.name.startsWith(".") } ?: emptyList()
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
        // Cenas vivem em <workspace>/<PROJETO>/scenes (§8.1 — scenesRoot do
        // projeto), não em <workspace>/scenes. O nome do projeto é a fonte
        // da verdade (o mesmo que o documento C++ resolve via ProjectPaths).
        val project = EditorJni.nativeEditorProjectName(handle)
        if (project.isNullOrEmpty()) {
            toast("Nenhum projeto aberto")
            return
        }
        val scenesRoot = File(File(File(filesDir, "projects"), project), "scenes")
        val files = scenesRoot.listFiles()?.filter { it.isFile } ?: emptyList()
        if (files.isEmpty()) {
            toast("Nenhuma cena salva em ${project}/scenes")
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

    /** ADD → Sprite (P1.10): um toque = entidade com SpriteData default,
     * selecionada. Sem textura → placeholder xadrez no viewport. */
    private fun addSpriteDialog() {
        inputDialog("Nome do sprite", "Sprite") { name ->
            val packed = EditorJni.nativeEditorCreateSprite(handle, name)
            if (packed == 0L) {
                toast(lastErrorText())
            } else {
                selectEntity(packed)
                toast("Sprite criado — importe uma imagem e escolha a textura no Inspector")
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

    /** Adicionar componente COM BUSCA (P0-6): filtra o catálogo ao digitar. */
    private fun addComponentDialog() {
        // P2 (§2/§14): catálogo ADDÁVEL à entidade (sem os presentes/built-ins)
        // + hint de dependência por tipo — o autor sabe o que falta ANTES de
        // adicionar. Nada de componentes falsos: vem do registro REAL do
        // serializer (Reflection → Inspector).
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

        val search = EditText(this).apply {
            hint = "Buscar componente…"
            setSingleLine()
            setPadding(dp(16), dp(10), dp(16), dp(10))
        }
        val list = ListView(this)
        val adapter = ArrayAdapter(
            this, android.R.layout.simple_list_item_1, mutableListOf<String>()
        )
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
            addView(search)
            addView(
                list,
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, dp(300)
                )
            )
        }
        var dialog: AlertDialog? = null
        list.setOnItemClickListener { _, _, which, _ ->
            if (!EditorJni.nativeEditorAddComponent(handle, selection, current[which])) {
                toast(lastErrorText())
            }
            dialog?.dismiss()
            refreshPanel()
        }
        dialog = AlertDialog.Builder(this)
            .setTitle("Adicionar componente")
            .setView(container)
            .setNegativeButton("Cancelar", null)
            .show()
    }

    private fun assetMenuDialog(asset: AssetEntry) {
        val category = assetCategory.selectedItem?.toString() ?: ""
        val items = if (category == "audio") {
            arrayOf("▶ Ouvir (preview)", "Renomear…", "Mover para…", "Apagar")
        } else if (category == "materials") {
            arrayOf("✎ Editar material…", "Renomear…", "Mover para…", "Apagar")
        } else {
            arrayOf("Renomear…", "Mover para…", "Apagar")
        }
        AlertDialog.Builder(this)
            .setTitle(asset.name)
            .setItems(items) { _, which ->
                if (category == "materials") {
                    when (which) {
                        0 -> editMaterialDialog(asset.name)
                        1 -> inputDialog("Novo nome", asset.name) { name ->
                            if (!EditorJni.nativeEditorAssetRename(handle, category, asset.name, name)) {
                                toast(lastErrorText())
                            }
                            refreshAssets()
                        }
                        2 -> moveAssetDialog(asset)
                        3 -> if (EditorJni.nativeEditorMaterialDelete(handle, asset.name)) {
                            refreshAssets()
                        } else {
                            toast(lastErrorText())
                        }
                    }
                    return
                }
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
        // P2 §17 — SAF (pasta de exportação / zip ida-e-volta).
        handleSafResult(requestCode, resultCode, data?.data)
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
            btnPlay.setTextColor(Ui.OK)
            toast("STOP — edição intacta")
        } else {
            if (EditorJni.nativeEditorPlay(handle)) {
                btnPlay.text = "■"
                btnPlay.setTextColor(Ui.DANGER)
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

    /** Menu de ferramentas (P1.6): SELECT / MOVE / ROTATE / SCALE. Pan e
     * zoom continuam gestos ALWAYS-ON (drag em espaço vazio / pinch). */
    private fun showToolMenu() {
        val labels = arrayOf(
            "Selecionar", "Mover (gizmo)", "Rotacionar (gizmo)", "Escalar (gizmo)"
        )
        val current = EditorJni.nativeEditorGetTool(handle)
        AlertDialog.Builder(this)
            .setTitle("Ferramenta")
            .setSingleChoiceItems(labels, current) { dialog, which ->
                editorTool = which
                EditorJni.nativeEditorSetTool(handle, which)
                btnTool.text = toolLabel(which)
                dialog.dismiss()
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    private fun toolLabel(tool: Int): String = when (tool) {
        1 -> "MOVER"; 2 -> "ROTAC"; 3 -> "ESCALA"; else -> "SELECT"
    }

    // --- gestos do viewport (§8.6/§8.8 — eventos do EDITOR, não do jogo) ------------------

    /**
     * Em PLAY (sem ferramenta ativa), os toques do viewport vão ao INPUT DO
     * JOGO (§6.4); a câmera do editor exige a ferramenta PAN/MOVER —
     * separação explícita editor×jogo.
     */
    private fun gameWantsTouch(): Boolean =
        handle != 0L && EditorJni.nativeEditorIsPlaying(handle) &&
            editorTool == 0

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
                    if (gizmoDragging) return true  // drag de gizmo ≠ tap (P1)
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
                    if (gizmoDragging) return true  // gizmo já consome (P1.3-5)
                    if (editorTool == 1 && selection != 0L) {
                        // MOVE a entidade selecionada (edit: dirty; play: clone).
                        EditorJni.nativeEditorMoveEntity(handle, selection, dx, dy)
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
                handleGizmoTouch(event)  // P1: raw events p/ drag de gizmo
                scaleDetector.onTouchEvent(event)
                tapDetector.onTouchEvent(event)
                true
            }
        }
    }

    /**
     * Drag do GIZMO (P1.3–P1.5) — eventos CRUS: o GestureDetector entrega
     * apenas DELTAS de scroll; rotação/escala precisam da posição
     * ABSOLUTA do pointer. Handles têm PRIORIDADE sobre o corpo da
     * entidade (P1.6): ACTION_DOWN pergunta ao documento; se um handle
     * acertou, o drag é do gizmo até o UP — tap/scroll ficam suprimidos.
     */
    private var gizmoDragging = false

    private fun handleGizmoTouch(event: MotionEvent) {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                gizmoDragging = false
                if (editorTool != 0 && selection != 0L) {
                    val handleId = EditorJni.nativeEditorGizmoDragBegin(
                        handle, event.x, event.y
                    )
                    gizmoDragging = handleId != 0
                }
            }
            MotionEvent.ACTION_MOVE -> if (gizmoDragging) {
                if (!EditorJni.nativeEditorGizmoDragTo(handle, event.x, event.y)) {
                    gizmoDragging = false  // erro: entidade morreu no drag
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> if (gizmoDragging) {
                EditorJni.nativeEditorGizmoDragEnd(handle)
                gizmoDragging = false
                refreshInspectorIfOpen()  // P1.9: campos finais do drag
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
        // P3 §0: a última operação viva antes de qualquer crash de
        // surface/render fica no logcat (dump ANTES de criar o renderer).
        EditorJni.nativeEditorDumpState(handle, "surfaceCreated")
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
            // Live sync P1.9: poll da revisão — gizmo/inspector/viewport
            // nunca divergem (fonte de verdade: o ECS do documento).
            val revision = EditorJni.nativeEditorSelectionRevision(handle)
            if (revision != lastSelectionRevision) {
                lastSelectionRevision = revision
                val nativeSel = EditorJni.nativeEditorSelection(handle)
                if (nativeSel != selection) {
                    selection = nativeSel  // play/stop/dup/delete mudaram
                    refreshHierarchySafe()
                    if (activePanel == PANEL_INSPECTOR) refreshInspector()
                }
                updateTransformFieldsLive()
            }
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
                // Densidade de editor: tipo à frente + indentação por
                // profundidade (evolução P0-4 — hierarquia LEGÍVEL).
                val icon = if (row.packed == selection) "▶ " else "· "
                text = "${"  ".repeat(row.depth)}$icon${row.name}"
                setTextColor(if (row.packed == selection) Ui.ACCENT else Ui.TEXT)
                setPadding(dp(8) + row.depth * dp(10), dp(9), dp(8), dp(9))
                minHeight = dp(38)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
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

        fun reload(lines: List<String>) {
            entries.clear()
            for (line in lines) {
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
            val entry = entries[position]
            val category = assetCategory.selectedItem?.toString() ?: ""
            // Linha densa com THUMBNAIL REAL (P0-6): imagens mostram o
            // conteúdo; demais tipos mostram o nome + id/metadata.
            val row = LinearLayout(this@EditorActivity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(dp(8), dp(6), dp(8), dp(6))
            }
            if (category == "textures") {
                val thumb = thumbnailOf("textures", entry.name)
                if (thumb != null) {
                    row.addView(
                        ImageView(this@EditorActivity).apply {
                            setImageBitmap(thumb)
                            scaleType = android.widget.ImageView.ScaleType.FIT_CENTER
                            background = rippleBox(Ui.BORDER, dp(4))
                            clipToOutline = true
                        },
                        LinearLayout.LayoutParams(dp(40), dp(40))
                    )
                    row.addView(
                        Space(this@EditorActivity),
                        LinearLayout.LayoutParams(dp(8), dp(1))
                    )
                }
            }
            val texts = LinearLayout(this@EditorActivity).apply {
                orientation = LinearLayout.VERTICAL
            }
            val imageInfo =
                if (category == "textures") {
                    EditorJni.nativeEditorAssetImageInfo(
                        handle, "textures", entry.name
                    )
                } else null
            texts.addView(TextView(this@EditorActivity).apply {
                text = entry.name
                setTextColor(if (entry.registered) Ui.TEXT else Ui.TEXT_DIM)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
                typeface = android.graphics.Typeface.DEFAULT_BOLD
            })
            texts.addView(TextView(this@EditorActivity).apply {
                text = when {
                    imageInfo != null -> imageInfo
                    entry.registered -> "id ${entry.id.take(8)}…"
                    else -> "(não catalogado)"
                }
                setTextColor(Ui.TEXT_DIM)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            })
            row.addView(
                texts,
                LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
            )
            row.setOnClickListener {
                selected = entry
                assetPreviewDialog(entry)
            }
            row.setOnLongClickListener {
                selected = entry
                assetMenuDialog(entry)
                true
            }
            return row
        }
    }

    /** Preview do asset: imagem REAL decodificada em bitmap (thumbnails
     * nativos do Android — evolução P0: duplo-toque MOSTRA o conteúdo). */
    private fun assetPreviewDialog(entry: AssetEntry) {
        val category = assetCategory.selectedItem?.toString() ?: return
        val info = EditorJni.nativeEditorAssetImageInfo(handle, category, entry.name)
        val project = EditorJni.nativeEditorProjectName(handle) ?: ""
        val file = File(File(File(filesDir, "projects"), project),
                        "assets/$category/${entry.name}")
        val message = StringBuilder("id: ${entry.id}\nregistrado: ${entry.registered}\n" +
                            "caminho: ${entry.path}")
        if (info != null) {
            message.append("\nimagem: $info")
        }
        val builder = AlertDialog.Builder(this)
            .setTitle(entry.name)
            .setMessage(message.toString())
            .setPositiveButton("OK", null)
        if (info != null && file.isFile) {
            val bitmap = android.graphics.BitmapFactory.decodeFile(file.absolutePath)
            if (bitmap != null) {
                val preview = ImageView(this).apply {
                    adjustViewBounds = true
                    scaleType = android.widget.ImageView.ScaleType.FIT_CENTER
                    setImageBitmap(bitmap)
                    setPadding(dp(16), dp(16), dp(16), dp(16))
                }
                builder.setView(preview)
            }
        }
        builder.show()
    }

    companion object {
        private const val PANEL_NONE = 0
        private const val PANEL_HIERARCHY = 1
        private const val PANEL_INSPECTOR = 2
        private const val PANEL_ASSETS = 3
        private const val PANEL_SCRIPTS = 4
        private const val PANEL_ANIM = 5
        private const val REQUEST_IMPORT = 4101
    }
}
