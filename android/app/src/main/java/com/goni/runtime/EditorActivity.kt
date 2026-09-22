package com.goni.runtime

import android.app.Activity
import android.content.Intent
import android.graphics.Typeface
import android.net.Uri
import android.os.Bundle
import android.text.InputType
import android.util.TypedValue
import android.view.Choreographer
import android.view.GestureDetector
import android.view.Gravity
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.os.Handler
import android.os.Looper
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.ScrollView
import android.widget.Space
import android.widget.TextView
import android.content.Context
import java.io.File
import java.util.zip.ZipEntry
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
 *
 * P4.5 — RECONSTRUÇÃO "CURVED DARK": toda a camada visual obedece ao
 * sistema de design Oni (OniUi.kt — tokens §1 do prompt). Zero mudança de
 * comportamento (contrato §3): B-B (foco/IME — sync diferencial), N1
 * (preview de áudio com stop), N2 (painel adaptativo via LayoutParams,
 * NUNCA detach) preservados linha a linha. A superfície separou-se em:
 *   EditorActivity.kt — ciclo de vida + chrome flutuante + plumagem
 *   EditorPanels.kt   — conteúdo dos painéis (hierarquia/inspector/...)
 *   EditorDialogs.kt  — menus/pickers (OniDialog — z zero diálogos default))
 *   ScriptWindow.kt   — janela dedicada de script (Bloco B)
 * Membros marcados `internal` são o contrato ENTRE estes ficheiros do
 * MESMO módulo — nada é público fora do pacote.
 */
class EditorActivity : Activity(), SurfaceHolder.Callback2,
    Choreographer.FrameCallback {

    internal var handle: Long = 0L
    private var choreographer: Choreographer? = null
    private var surfaceReady = false
    private var lastFrameNanos = 0L
    // P3.5 (T2): o FIRST_TRAVERSAL é emitido UMA vez (primeiro doFrame).
    private var firstTraversalMarked = false

    // UI
    internal lateinit var surfaceView: SurfaceView
    internal lateinit var rootLayout: FrameLayout
    internal lateinit var topBar: LinearLayout
    /// P4.5: `bottomBar` passou a ser o CHROME INFERIOR inteiro (linha de
    /// ferramentas flutuante + tab bar card) num wrapper vertical — o painel
    /// continua a âncorar acima dele por LayoutParams (N2 intocado).
    internal lateinit var bottomBar: LinearLayout
    internal lateinit var brand: TextView
    internal lateinit var panelHost: FrameLayout
    internal lateinit var panelContainer: LinearLayout
    private var panelScrim: View? = null
    /// P4.5: guard da animação do sheet (evita GONE atrasado sobre
    /// painel novo — troca rápida de tabs durante o slide de saída).
    private var sheetShowing = false
    private lateinit var tabButtons: List<TextView>
    internal lateinit var btnPlay: android.widget.Button
    /// P4.5: chips são TextView (eram controles default).
    internal lateinit var btnProject: TextView
    internal lateinit var btnBackend: TextView
    internal lateinit var toolSegments: List<TextView>
    internal lateinit var hierarchyList: ListView
    internal lateinit var inspectorScroll: ScrollView
    /// P4.1 (T2/D5 + T3/D6): HUD do Play — scripts + áudio, texto HONESTO
    /// em device (o silêncio calado era o D5/D6).
    internal lateinit var playHud: TextView
    private var hudFrameCounter = 0L
    /// P4.5: categoria de assets — chip pill que abre picker (era dropdown default).
    internal var assetCategoryName: String = ""
    internal lateinit var assetList: ListView
    internal lateinit var hierarchyAdapter: HierarchyAdapter
    internal lateinit var assetAdapter: AssetAdapter

    internal var editorTool = 0 // 0=Select 1=Move 2=Rotate 3=Scale (C++ manda)
    internal var activePanel = PANEL_NONE

    // Live sync (P1.9): últimos valores de transform exibidos no Inspector.
    internal var transformFields: MutableList<EditText> = mutableListOf()
    internal var collectTransformFields = false
    private var lastSelectionRevision = -1L

    // P4.2 (B-B — teclado que abre e fecha): sync DIFERENCIAL do Inspector.
    // A estrutura (componentes/campos/kinds) tem assinatura; mudou → rebuild;
    // mesma estrutura → valores in-place (nunca recria view, nunca toca na
    // view com FOCO — o IME sobrevive).
    internal var inspectorKey: String? = null
    internal var inspectorContent: LinearLayout? = null
    internal var inspectorNameField: EditText? = null
    internal val inspectorTextFields = mutableMapOf<String, EditText>() // "comp\u0001path"
    internal val inspectorValueViews =
        mutableMapOf<String, Pair<TextView, String>>()    // view + placeholder
    // P4.6 (Bloco 1): rows de bitfield nomeado (chips) p/ sync diferencial
    // — mesmo contrato do B-B: valor in-place, focado nunca é tocado.
    internal val inspectorBitfieldRows = mutableMapOf<String, LinearLayout>()

    // P4.2 (T5 — Modo Jogo G1): chrome escondido + HUD fullscreen.
    internal var gameMode = false
    private var gameHudBar: LinearLayout? = null
    private var gameHudStatus: TextView? = null
    private var btnPauseGame: TextView? = null
    private var fpsFrames = 0
    private var fpsAccum = 0f
    private var fpsShown = 0f

    // Painel de scripts (P0-7): lista carregada por refreshScripts().
    internal var scriptsList: ListView? = null
    internal val scriptNames = mutableListOf<String>()

    // Campos dos painéis vivos (preenchidos pelos builders em EditorPanels.kt).
    internal var ticksList: ListView? = null
    internal var animList: ListView? = null
    internal var assetsRoot: LinearLayout? = null
    internal var assetSearch: EditText? = null
    internal var assetCategoryChip: TextView? = null
    internal var assetQuery: String = ""
    internal val tickLayerRows = mutableListOf<LayerRow>()
    internal val animNames = mutableListOf<String>()

    internal var importButton: android.widget.Button? = null
    internal var importTmpDir: File? = null

    // P4.5 (Bloco B/C): zoom cluster, undo/redo FABs, snap chips.
    internal var zoomCluster: LinearLayout? = null
    internal var undoCluster: LinearLayout? = null
    private var btnUndo: TextView? = null
    private var btnRedo: TextView? = null
    private var snapGradeChip: TextView? = null
    private var snapAngleChip: TextView? = null

    // --- ciclo de vida ---------------------------------------------------------

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        // P3.2: espelho de diagnóstico ANTES de qualquer coisa — mesmo que
        // o editor (ou a própria lib nativa) morra em seguida, a cópia
        // pública em Download/GONI/ já existe com o que houve até aqui.
        DiagnosticsMirror.init(this)
        // P4.5.1: handler de exceções Kotlin — instalado ANTES de qualquer
        // código que possa lançar. Exceções Kotlin tinham ZERO forense
        // (o handler nativo só apanha sinais): 5 mortes silenciosas no
        // device provaram a lacuna. Delega ao handler anterior — a morte
        // do sistema é intacta; agora COM evidência (goni_crash.log).
        KotlinCrashGuard.install(this)
        KotlinCrashGuard.phase("BOOTSTRAP")
        try {
            // P3.1 (FASE 4/5): diagnóstico persistente ANTES de qualquer
            // subsistema — cada estágio daqui para frente é gravado na HORA
            // em filesDir/goni_startup.log (sobrevive à morte do processo) e
            // o crash handler nativo grava goni_crash.log antes do tombstone.
            // P3.2: cada estágio persistido também reescreve a cópia
            // pública (Download/GONI/goni_startup.log) via MediaStore.
            // P3.5: o espelho é ASSÍNCRONO (fila + worker background) — a
            // main thread nunca mais espera MediaStore no caminho de mark.
            EditorJni.bootstrap(this)
        } catch (t: Throwable) {
            // A lib nativa pode nem carregar (dlopen/UnsatisfiedLinkError/
            // ExceptionInInitializerError): sem este registro a morte seria
            // indistinguível de "abre e fecha" sem evidência NENHUMA.
            DiagnosticsMirror.recordBootstrapFailure(t)
            android.util.Log.e("GONI", "bootstrap nativo falhou", t)
            toastErr("Falha ao iniciar o engine: ${t.message}")
            finish()
            return
        }
        // P3.5 (T3): watchdog de hang da main thread (ver P3.5 — graça 15 s).
        Watchdog.start()
        // P3.2: crash de execução ANTERIOR → exporta IMEDIATAMENTE.
        if (EditorJni.nativeStartupHasCrashReport()) {
            DiagnosticsMirror.exportCrashLogIfPresent()
        }
        EditorJni.nativeStartupMark("STARTUP_APPLICATION", "ok", "process")
        EditorJni.nativeStartupMark("STARTUP_ACTIVITY", "ok", "EditorActivity")
        // P4.5.1 (R3 — JANELA DA MORTE #2: ACTIVITY → EDITOR_HOST): a
        // sessão pid 24646 morreu AQUI dentro sem nomear a fase. Estes
        // dois marks auditam o TEMA/SPLASH NO DEVICE e delimitam todo o
        // resto da janela — incluindo o crash-prompt (OniDialog, código
        // P4.5 que só roda quando há crash report anterior) e a entrada
        // JNI do create. Se a próxima morte cair aqui, o último mark
        // NOMEIA a fase — e o KotlinCrashGuard nomeia a LINHA.
        KotlinCrashGuard.phase("UI_THEME")
        markUiTheme()
        KotlinCrashGuard.phase("UI_SPLASH")
        markUiSplash()
        KotlinCrashGuard.phase("CRASH_PROMPT")
        maybeOfferCrashExport()

        // Workspace: filesDir/projects (interno — sem permissões). Paths
        // DENTRO do projeto seguem relativos (§8.1 — ADR-032).
        KotlinCrashGuard.phase("EDITOR_CREATE")
        val workspace = File(filesDir, "projects").apply { mkdirs() }
        handle = EditorJni.nativeEditorCreate("auto", workspace.absolutePath)
        if (handle == 0L) {
            toastErr("Falha ao criar o editor (ver logcat)")
            finish()
            return
        }

        // P4.5.1 (R3 — JANELA DA MORTE #1: pós-EDITOR_DOCUMENT → UI P4.5):
        // as 4 sessões (pid 24087…24618) morreram EXATAMENTE aqui dentro
        // (build da UI nova — OniUi/sheets/1º frame) sem NENHUM mark entre
        // STARTUP_EDITOR_DOCUMENT e STARTUP_EDITOR_UI. Quatro micro-marks
        // cobrem agora o intervalo inteiro; nada mais mudou.
        EditorJni.nativeStartupMark("UI_BUILD_START", "ok",
            "buildUi começa (P4.5 Curved Dark)")
        KotlinCrashGuard.phase("UI_BUILD")
        buildUi()
        EditorJni.nativeStartupMark("STARTUP_EDITOR_UI", "ok", "UI construída")
        KotlinCrashGuard.phase("POST_PROJECT")
        ensureProjectOnFirstRun()
        EditorJni.nativeStartupMark("STARTUP_POST_PROJECT", "ok", "ensureProject retornou")
        KotlinCrashGuard.phase("UI_SYNC")
        refreshAll()
        EditorJni.nativeStartupMark("STARTUP_UI_SYNC", "ok", "refreshAll concluído")
        // P2 (§5): o DOCUMENTO é a fonte da verdade — a ferramenta da UI
        // sincroniza com a nativa (activity recriada não diverge).
        editorTool = EditorJni.nativeEditorGetTool(handle)
        updateToolSegments()
        // P4.1 (T1/D3/D4): densidade do device → alvos de toque do gizmo.
        KotlinCrashGuard.phase("UI_SCALE")
        installUiScale()
        KotlinCrashGuard.phase("IDLE")
    }

    /**
     * P4.5.1 (R2/R3): auditoria do TEMA no device real — resolve o
     * windowBackground do tema ativo e confirma que é o splash Oni, e
     * que o ícone do launcher resolve (no Android 12+ o splash do
     * SISTEMA usa o ícone do launcher — um adaptive-icon quebrado matava
     * ANTES de qualquer onCreate nosso). Auditoria apenas: qualquer
     * falha é REGISTRADA (mark failed) — nunca aborta o startup.
     */
    private fun markUiTheme() {
        val outcome = try {
            val tv = TypedValue()
            val resolved = theme.resolveAttribute(
                android.R.attr.windowBackground, tv, true)
            val bgIsSplash = resolved && tv.resourceId != 0 &&
                tv.resourceId == R.drawable.oni_splash
            // Ícone do launcher resolve? (usado pelo splash do sistema 12+)
            val icon = resources.getDrawable(R.mipmap.ic_launcher, theme)
            "ok" to "windowBackground splash=$bgIsSplash, launcher=${icon != null}"
        } catch (t: Throwable) {
            "failed" to "${t.javaClass.simpleName}: ${t.message}"
        }
        EditorJni.nativeStartupMark("UI_THEME", outcome.first, outcome.second)
    }

    /**
     * P4.5.1 (R2/R3): auditoria da CADEIA do splash "Curved Dark" no
     * device — infla o layer-list (fundo oni_bg + logo 96dp + wordmark
     * por densidade). Resolve o caminho completo de recursos que a
     * janela usa antes do primeiro frame. Best-effort igual acima.
     */
    private fun markUiSplash() {
        val outcome = try {
            val splash = resources.getDrawable(R.drawable.oni_splash, theme)
            "ok" to "layer-list inflado (${splash.javaClass.simpleName})"
        } catch (t: Throwable) {
            "failed" to "${t.javaClass.simpleName}: ${t.message}"
        }
        EditorJni.nativeStartupMark("UI_SPLASH", outcome.first, outcome.second)
    }

    /** P4.1 (T1): instala a densidade no viewport nativo (dp → px). */
    private fun installUiScale() {
        if (handle != 0L) {
            EditorJni.nativeEditorSetUiScale(
                handle, resources.displayMetrics.density
            )
        }
    }

    override fun onResume() {
        super.onResume()
        if (handle != 0L) {
            EditorJni.nativeEditorOnResume(handle)
        }
        // P3.5 (T2) — micro-marks da janela resume→surface.
        EditorJni.nativeStartupMark("MIRROR_ENQUEUE", "ok",
            "espelho assíncrono (pós-resume)")
        lastFrameNanos = 0L
        choreographer = Choreographer.getInstance().also { it.postFrameCallback(this) }
        EditorJni.nativeStartupMark("RESUME_RETURN", "ok", "choreographer armado")
        // LOOPER_IDLE só aparece quando a main thread processa a PRÓXIMA
        // mensagem — se a main travar dentro do resume/traversal, este
        // mark é o primeiro que NÃO aparece (evidence por ausência).
        Handler(Looper.getMainLooper()).post {
            EditorJni.nativeStartupMark("LOOPER_IDLE", "ok",
                "main thread processa mensagens")
        }
        EditorJni.nativeWatchdogHeartbeat()
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

    internal fun dp(v: Int): Int = Oni.dp(this, v)

    /** Label flutuante (texto dim 12sp) — wrapper p/ painéis. */
    internal fun labelView(text: String): TextView =
        Oni.rowText(this, text, dim = true, sizeSp = 12f).apply {
            setPadding(dp(4), dp(8), dp(4), dp(4))
        }

    /** Header de secção (uppercase 12sp — §1.5) sobre tom, sem borda. */
    internal fun sectionTitle(text: String): TextView =
        Oni.sectionHeader(this, text)

    private fun buildUi() {
        // Viewport (fundo) + chrome por cima.
        surfaceView = SurfaceView(this)
        surfaceView.holder.addCallback(this)
        surfaceView.holder.setKeepScreenOn(true)
        attachGestures(surfaceView)

        val root = FrameLayout(this)
        rootLayout = root
        root.setBackgroundColor(Oni.BG)
        // P4.3 (N2 — IME): o root ENCOLHE quando o teclado abre (adjustResize)
        // — este listener reage e redimensiona o PAINEL por LayoutParams
        // (nunca re-parent: o B-B — foco/IME intocáveis — não pode voltar).
        root.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
            updatePanelHeight()
        }

        // ---- header card flutuante (§2): marca + projeto | cena | backend | play
        topBar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            background = Oni.rounded(this@EditorActivity, Oni.PANEL, Oni.R_CARD)
            setPadding(dp(12), dp(8), dp(12), dp(8))
            gravity = Gravity.CENTER_VERTICAL
        }
        brand = TextView(this).apply {
            text = "G.ONI"
            setTextColor(Oni.ACCENT)
            typeface = Typeface.DEFAULT_BOLD
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            setPadding(0, 0, dp(8), 0)
        }
        topBar.addView(brand)
        btnProject = Oni.chip(this, "☰", textSizeSp = 14f).also {
            it.setOnClickListener { showProjectMenu() }
        }
        topBar.addView(btnProject, LinearLayout.LayoutParams(dp(48), dp(48)))
        topBar.addView(
            Oni.chip(this, "Cena", textSizeSp = 13f).also {
                it.setOnClickListener { showSceneMenu() }
            },
            LinearLayout.LayoutParams(0, dp(48), 0.9f)
        )
        // Play = icon-button ACENTO circular (§2 — chamada do acento).
        btnPlay = Oni.button(this, "▶", kind = Oni.BTN_PRIMARY, textSizeSp = 16f)
        btnPlay.setOnClickListener { togglePlay() }
        topBar.addView(btnPlay, LinearLayout.LayoutParams(dp(48), dp(48)))
        btnBackend = Oni.chip(this, "auto", mono = true, textSizeSp = 12f).also {
            it.setOnClickListener { b -> showBackendMenu(b as TextView) }
        }
        topBar.addView(btnBackend, LinearLayout.LayoutParams(0, dp(48), 0.7f))
        // P4.5.1 (R3): header card completo (Oni.chip/Oni.button/tokens) —
        // o primeiro trecho P4.5-escrito do build agora tem nome e sobrevive
        // ao processo (mark persistido na hora).
        KotlinCrashGuard.phase("ONIUI_INIT")
        EditorJni.nativeStartupMark("ONIUI_INIT", "ok",
            "header + chips + play (tokens Oni) construídos")

        // ---- chrome inferior: linha de ferramentas flutuante + tab bar card ----
        bottomBar = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }
        val chromeRow = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
        }

        // Tool switcher — segmented pill flutuante (§2), thumb zone.
        val toolPill = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            background = Oni.pill(this@EditorActivity, 0xF21A2029.toInt())
            setPadding(dp(4), dp(4), dp(4), dp(4))
        }
        val segments = listOf("Select" to 0, "Move" to 1, "Rotation" to 2, "Scale" to 3)
        toolSegments = segments.map { (label, tool) ->
            Oni.chip(this, label, textSizeSp = 12f).also { seg ->
                seg.minHeight = dp(48)
                seg.setPadding(dp(12), 0, dp(12), 0)
                seg.setOnClickListener {
                    editorTool = tool
                    EditorJni.nativeEditorSetTool(handle, tool)
                    updateToolSegments()
                }
            }
        }
        for (seg in toolSegments) {
            toolPill.addView(seg, LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, dp(48), 1f))
        }
        chromeRow.addView(toolPill, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT
        ))

        // Snap chips (§2 — toggleáveis): grade 0.5u / ângulo 15°.
        val snapRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            background = Oni.pill(this@EditorActivity, 0xE61A2029.toInt())
            setPadding(dp(4), dp(4), dp(4), dp(4))
        }
        snapGradeChip = Oni.chip(this, "Grade", textSizeSp = 11f).apply {
            minimumHeight = dp(48)
            setOnClickListener { toggleSnap(translate = true) }
        }
        snapAngleChip = Oni.chip(this, "15°", mono = true, textSizeSp = 11f).apply {
            minimumHeight = dp(48)
            setOnClickListener { toggleSnap(translate = false) }
        }
        snapRow.addView(snapGradeChip, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, dp(48)))
        snapRow.addView(snapAngleChip, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, dp(48)))
        chromeRow.addView(snapRow, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT
        ).apply { topMargin = dp(6) })

        // Tab bar card (§2): ativa = pill filled acento.
        val tabCard = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            background = Oni.rounded(this@EditorActivity, Oni.PANEL, Oni.R_CARD)
            setPadding(dp(6), dp(6), dp(6), dp(6))
            gravity = Gravity.CENTER_VERTICAL
        }
        val tabs = listOf(
            "Hierarquia" to PANEL_HIERARCHY,
            "Inspector" to PANEL_INSPECTOR,
            "Assets" to PANEL_ASSETS,
            "Scripts" to PANEL_SCRIPTS,
            "Animação" to PANEL_ANIM,
            "Ticks" to PANEL_TICKS
        )
        tabButtons = tabs.map { (label, panel) ->
            Oni.chip(this, label, textSizeSp = 11f).also { tab ->
                tab.minHeight = dp(48)
                tab.setPadding(dp(4), 0, dp(4), 0)
                tab.setOnClickListener { togglePanel(panel) }
            }
        }
        for (tab in tabButtons) {
            tabCard.addView(tab, LinearLayout.LayoutParams(
                0, dp(48), 1f))
        }

        bottomBar.addView(chromeRow, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT
        ).apply { setMargins(dp(8), 0, dp(8), dp(8)) })
        bottomBar.addView(tabCard, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT
        ).apply { setMargins(dp(8), 0, dp(8), dp(8)) })

        // ---- host de painel (sheet inferior / drawer lateral) + scrim ----
        panelHost = FrameLayout(this)
        panelScrim = View(this).apply {
            setBackgroundColor(Oni.SCRIM)
            visibility = View.GONE
            setOnClickListener { if (activePanel != PANEL_NONE) togglePanel(activePanel) }
        }
        panelHost.addView(panelScrim, FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT
        ))

        // P4.1 (T2/D5 + T3/D6): HUD do PLAY — pill translúcida (§2).
        playHud = TextView(this).apply {
            setTextColor(Oni.TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            typeface = Typeface.MONOSPACE
            background = Oni.rounded(this@EditorActivity, 0xCC12161D.toInt(), Oni.R_CARD)
            setPadding(dp(12), dp(8), dp(12), dp(8))
            visibility = View.GONE
        }

        root.addView(
            surfaceView,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
        // P4.5 (§2): zoom cluster pill vertical bottom-left (+/−/fit).
        zoomCluster = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = Oni.pill(this@EditorActivity, 0xF21A2029.toInt())
            setPadding(dp(4), dp(4), dp(4), dp(4))
        }
        fun zoomChip(label: String, monoGlyph: Boolean, onClick: () -> Unit): TextView =
            Oni.chip(this, label, mono = monoGlyph, textSizeSp = 16f).apply {
                minimumWidth = dp(48)
                setOnClickListener { onClick() }
            }
        zoomCluster?.addView(zoomChip("+", true) { zoomBy(1.25f) },
            LinearLayout.LayoutParams(dp(48), dp(48)))
        zoomCluster?.addView(zoomChip("−", true) { zoomBy(0.8f) },
            LinearLayout.LayoutParams(dp(48), dp(48)))
        zoomCluster?.addView(zoomChip("⛶", false) { fitViewport() },
            LinearLayout.LayoutParams(dp(48), dp(48)))
        root.addView(zoomCluster, FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT,
            Gravity.BOTTOM or Gravity.START
        ).apply { setMargins(dp(10), 0, 0, dp(178)) })

        // P4.5 (§2): undo/redo — FABs circulares bottom-right (command
        // pattern NATIVO — snapshots de cena; disabled sem histórico).
        undoCluster = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }
        btnUndo = Oni.button(this, "↶", kind = Oni.BTN_GHOST, textSizeSp = 18f).apply {
            minimumWidth = dp(48)
            setOnClickListener { runUndo() }
        }
        btnRedo = Oni.button(this, "↷", kind = Oni.BTN_GHOST, textSizeSp = 18f).apply {
            minimumWidth = dp(48)
            setOnClickListener { runRedo() }
        }
        undoCluster?.addView(btnUndo, LinearLayout.LayoutParams(dp(48), dp(48)))
        undoCluster?.addView(View(this), LinearLayout.LayoutParams(1, dp(8)))
        undoCluster?.addView(btnRedo, LinearLayout.LayoutParams(dp(48), dp(48)))
        root.addView(undoCluster, FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT,
            Gravity.BOTTOM or Gravity.END
        ).apply { setMargins(0, 0, dp(10), dp(178)) })

        root.addView(
            panelHost,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
        root.addView(
            playHud,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM or Gravity.START
            ).apply {
                bottomMargin = dp(76)
                leftMargin = dp(12)
            }
        )
        // P4.2 (T5 — Modo Jogo G1): HUD do Play — pill flutuante translúcida:
        // STOP (danger) / PAUSE (warn) / estado mono (§2).
        gameHudBar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            background = Oni.pill(this@EditorActivity, 0xCC12161D.toInt())
            setPadding(dp(8), dp(6), dp(12), dp(6))
            gravity = Gravity.CENTER_VERTICAL
            visibility = View.GONE
        }
        gameHudBar?.addView(
            Oni.button(this, "■", kind = Oni.BTN_DANGER, textSizeSp = 14f).also {
                it.setTextColor(Oni.DANGER)
                it.setOnClickListener { togglePlay() }
            },
            LinearLayout.LayoutParams(dp(52), dp(48))
        )
        btnPauseGame = Oni.chip(this, "⏸", textSizeSp = 14f).also {
            it.setTextColor(Oni.WARN)
            it.setOnClickListener { togglePauseGame() }
        }
        gameHudBar?.addView(btnPauseGame, LinearLayout.LayoutParams(dp(48), dp(48)))
        gameHudStatus = TextView(this).apply {
            setTextColor(Oni.TEXT_DIM)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            typeface = Typeface.MONOSPACE
            setPadding(dp(8), 0, 0, 0)
        }
        gameHudBar?.addView(
            gameHudStatus,
            LinearLayout.LayoutParams(0, dp(48), 1.5f)
        )
        root.addView(
            gameHudBar,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM
            ).apply { setMargins(dp(12), 0, dp(12), dp(16)) }
        )
        root.addView(
            bottomBar,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM
            )
        )
        root.addView(
            topBar,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP
            ).apply { setMargins(dp(8), dp(8), dp(8), 0) }
        )

        panelContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            visibility = View.GONE
        }

        hierarchyAdapter = HierarchyAdapter()
        inspectorScroll = ScrollView(this).apply {
            background = null
        }
        buildAssetsPanel()
        // P4.5.1 (R3): sheets + scrim + adapters prontos — a metade pesada
        // do build P4.5 passou (é aqui que a UI nova mais criava views).
        KotlinCrashGuard.phase("UI_SHEETS")
        EditorJni.nativeStartupMark("UI_SHEETS", "ok",
            "sheets/scrim/adapters dos painéis construídos")

        setContentView(root)
        applyWindowInsets()
        updatePanelPlacement()
        updateToolSegments()
        updateTabStates()
        zoomCluster?.visibility = View.VISIBLE
        undoCluster?.visibility = View.VISIBLE
        syncSnapChips()
        updateUndoRedo()
        // P4.5.1 (R3): UI anexada à janela (traversal agendado) — o 1º frame
        // é a última zona cega do build; doFrame já marca FIRST_TRAVERSAL.
        KotlinCrashGuard.phase("UI_FIRST_FRAME")
        EditorJni.nativeStartupMark("UI_FIRST_FRAME", "ok",
            "setContentView + insets + estados iniciais concluídos")
    }

    /** Zoom por botão (cluster): fator no CENTRO do viewport. */
    private fun zoomBy(factor: Float) {
        if (handle == 0L) return
        val cx = surfaceView.width * 0.5f
        val cy = surfaceView.height * 0.5f
        EditorJni.nativeEditorViewportZoom(handle, factor, cx, cy)
    }

    /** Fit: enquadra a seleção (ou a cena inteira) — chamada nativa. */
    private fun fitViewport() {
        if (handle == 0L) return
        if (!EditorJni.nativeEditorViewportFit(handle)) {
            toastErr(lastErrorText())
        }
    }

    /** Chip de snap → nativo (fonte de verdade: o documento). */
    private fun toggleSnap(translate: Boolean) {
        if (handle == 0L) return
        if (translate) {
            EditorJni.nativeEditorSetSnap(
                handle, !EditorJni.nativeEditorGetSnapTranslate(handle),
                EditorJni.nativeEditorGetSnapRotate(handle))
        } else {
            EditorJni.nativeEditorSetSnap(
                handle, EditorJni.nativeEditorGetSnapTranslate(handle),
                !EditorJni.nativeEditorGetSnapRotate(handle))
        }
        syncSnapChips()
    }

    private fun syncSnapChips() {
        if (handle == 0L || snapGradeChip == null) return
        val g = EditorJni.nativeEditorGetSnapTranslate(handle)
        val r = EditorJni.nativeEditorGetSnapRotate(handle)
        snapGradeChip?.setTextColor(if (g) Oni.ACCENT else Oni.TEXT_DIM)
        snapAngleChip?.setTextColor(if (r) Oni.ACCENT else Oni.TEXT_DIM)
        snapGradeChip?.background =
            if (g) Oni.ripplePill(this, 0x2E8AB4F8.toInt()) else Oni.rippleOnly(this)
        snapAngleChip?.background =
            if (r) Oni.ripplePill(this, 0x2E8AB4F8.toInt()) else Oni.rippleOnly(this)
    }

    /** Undo (FAB): restaura o snapshot anterior — erros explícitos. */
    private fun runUndo() {
        if (handle == 0L) return
        if (EditorJni.nativeEditorUndo(handle)) {
            selection = 0L  // IDs mudaram — seleção morre honestamente
            refreshAll()
            refreshPanel()
            toast("Desfeito")
        } else {
            toastErr(lastErrorText())
        }
        updateUndoRedo()
    }

    private fun runRedo() {
        if (handle == 0L) return
        if (EditorJni.nativeEditorRedo(handle)) {
            selection = 0L
            refreshAll()
            refreshPanel()
            toast("Refeito")
        } else {
            toastErr(lastErrorText())
        }
        updateUndoRedo()
    }

    /** Estado visual dos FABs (disabled sem histórico — §2). */
    internal fun updateUndoRedo() {
        val canU = handle != 0L && EditorJni.nativeEditorCanUndo(handle)
        val canR = handle != 0L && EditorJni.nativeEditorCanRedo(handle)
        btnUndo?.isEnabled = canU
        btnRedo?.isEnabled = canR
        btnUndo?.alpha = if (canU) 1f else 0.35f
        btnRedo?.alpha = if (canR) 1f else 0.35f
    }

    /** Estado visual do segmented pill (ativo = acento — §1.4). */
    internal fun updateToolSegments() {
        if (!::toolSegments.isInitialized) return
        for ((i, seg) in toolSegments.withIndex()) {
            val active = i == editorTool
            seg.setTextColor(if (active) Oni.ON_ACCENT else Oni.TEXT_DIM)
            seg.background =
                if (active) Oni.ripplePill(this, Oni.ACCENT)
                else Oni.rippleOnly(this)
        }
    }

    /** Estado visual das tabs (ativa = pill filled acento — §2). */
    private fun updateTabStates() {
        if (!::tabButtons.isInitialized) return
        val tabs = listOf(
            PANEL_HIERARCHY, PANEL_INSPECTOR, PANEL_ASSETS,
            PANEL_SCRIPTS, PANEL_ANIM, PANEL_TICKS
        )
        for ((i, tab) in tabButtons.withIndex()) {
            val active = tabs[i] == activePanel && activePanel != PANEL_NONE
            tab.setTextColor(if (active) Oni.ON_ACCENT else Oni.TEXT_DIM)
            tab.background =
                if (active) Oni.ripplePill(this, Oni.ACCENT)
                else Oni.rippleOnly(this)
        }
    }

    /** Insets reais (status/nav bar — sem androidx): o chrome RESPETA o
     *  sistema em portrait e landscape (evolução P0-4). */
    private fun applyWindowInsets() {
        window.decorView.setOnApplyWindowInsetsListener { _, insets ->
            val top = insets.systemWindowInsetTop
            val bottom = insets.systemWindowInsetBottom
            val left = insets.systemWindowInsetLeft
            val right = insets.systemWindowInsetRight
            topBar.setPadding(dp(12) + left, dp(8) + top, dp(12) + right, dp(8))
            // tab card (último filho do chrome): respeita a nav bar.
            val tabCard = bottomBar.getChildAt(bottomBar.childCount - 1)
            tabCard.setPadding(dp(6) + left, dp(6), dp(6) + right, dp(6) + bottom)
            gameHudBar?.setPadding(dp(8) + left, dp(6), dp(12) + right, dp(6))
            updatePanelPlacement()
            insets
        }
    }

    private var panelPlacementLandscape = false

    /** Portrait: painel = sheet inferior (máx 62% da altura, viewport
     *  continua por trás). Landscape: drawer lateral direito (46%). */
    private fun updatePanelPlacement() {
        if (!::panelHost.isInitialized || !::panelContainer.isInitialized) return
        val isLandscape = resources.configuration.orientation ==
                android.content.res.Configuration.ORIENTATION_LANDSCAPE
        // P4.2 (B-B — CAUSA RAIZ do teclado que abre e fecha): este método
        // rodava a CADA dispatch de insets — e ABRIR O TECLADO dispara
        // insets (adjustResize) — re-parentando o panelContainer
        // (removeView+addView): o EditText focado era destacado da janela,
        // o IME fechava, a janela voltava a crescer, novo dispatch, novo
        // re-parent: LOOP. Agora só re-parenta quando o MODO muda de
        // verdade (rotação) — insets só ajustam padding das barras.
        if (panelContainer.parent != null &&
            panelPlacementLandscape == isLandscape) {
            return
        }
        panelPlacementLandscape = isLandscape
        (panelContainer.parent as? FrameLayout)?.removeView(panelContainer)
        if (isLandscape) {
            panelHost.addView(
                panelContainer,
                FrameLayout.LayoutParams(
                    (resources.displayMetrics.widthPixels * 0.46f).toInt(),
                    ViewGroup.LayoutParams.MATCH_PARENT
                ).apply {
                    gravity = Gravity.RIGHT or Gravity.BOTTOM
                    setMargins(dp(8), dp(8), dp(8), dp(8))
                }
            )
            // Drawer lateral: card curvo flutuante dos dois lados.
            panelContainer.background =
                Oni.rounded(this, Oni.PANEL, Oni.R_CARD)
        } else {
            panelHost.addView(
                panelContainer,
                FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    (resources.displayMetrics.heightPixels * 0.62f).toInt(),
                    Gravity.BOTTOM
                )
            )
            // Sheet inferior: topo 28dp (§1.2), base reta na aresta.
            panelContainer.background = Oni.roundedCorners(
                this, Oni.PANEL,
                floatArrayOf(28f, 28f, 28f, 28f, 0f, 0f, 0f, 0f)
            )
        }
    }

    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) {
        super.onConfigurationChanged(newConfig)
        // configChanges cobre orientation|screenSize — o layout ADAPTA em
        // runtime sem recriar a Activity (evolução P0-4).
        updatePanelPlacement()
        installUiScale()  // P4.1 (T1): densidade pode mudar com a config
    }

    // --- painéis ---------------------------------------------------------------

    internal fun togglePanel(panel: Int) {
        if (activePanel == panel) {
            activePanel = PANEL_NONE
            // P4.3 (N1): painel fechou — a voice de preview morre com o
            // contexto (nunca toca "para sempre" atrás da UI).
            stopAudioPreview()
            hideSheet()
            updateTabStates()
            return
        }
        if (activePanel != PANEL_NONE) {
            stopAudioPreview()  // troca de painel: mesmo contexto morto
        }
        activePanel = panel
        panelContainer.removeAllViews()

        // Drag-handle (§2) + cabeçalho do sheet: título 16sp + fechar 48dp.
        panelContainer.addView(View(this).apply {
            background = Oni.pill(this@EditorActivity, Oni.HANDLE)
        }, LinearLayout.LayoutParams(dp(36), dp(4)).apply {
            gravity = Gravity.CENTER_HORIZONTAL
            topMargin = dp(10)
            bottomMargin = dp(4)
        })
        val title = when (panel) {
            PANEL_HIERARCHY -> "Hierarquia"
            PANEL_INSPECTOR -> "Inspector"
            PANEL_ASSETS -> "Assets"
            PANEL_SCRIPTS -> "Scripts"
            PANEL_ANIM -> "Animação"
            PANEL_TICKS -> "Ticks & Camadas"
            else -> ""
        }
        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(dp(16), dp(4), dp(8), dp(8))
            gravity = Gravity.CENTER_VERTICAL
        }
        header.addView(
            TextView(this).apply {
                text = title
                setTextColor(Oni.TEXT)
                typeface = Typeface.DEFAULT_BOLD
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            },
            LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        )
        header.addView(
            Oni.chip(this, "✕", textSizeSp = 13f).also {
                it.minimumWidth = dp(48)
                it.setOnClickListener { togglePanel(panel) }
            },
            LinearLayout.LayoutParams(dp(48), dp(48))
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
            PANEL_TICKS -> buildTicksPanel()
        }
        showSheet()
        updateTabStates()
        // P4.3 (N2): altura ADAPTATIVA (viewport atual, teclado incluído) —
        // via LayoutParams (sem detach); ver updatePanelHeight.
        updatePanelHeight()
        refreshPanel()
    }

    /** Sheet entra: scrim fade + slide 180 ms (§1.6) — SEM detach. */
    private fun showSheet() {
        sheetShowing = true
        panelScrim?.let { s ->
            s.visibility = View.VISIBLE
            s.alpha = 0f
            s.animate().alpha(1f).setDuration(180).start()
        }
        panelContainer.visibility = View.VISIBLE
        panelContainer.post {
            panelContainer.translationY =
                (panelContainer.height * 0.12f).coerceAtLeast(dp(24).toFloat())
            panelContainer.animate()
                .translationY(0f)
                .setDuration(180)
                .start()
        }
    }

    /** Sheet sai: slide curto → GONE (idempotente). */
    private fun hideSheet() {
        sheetShowing = false
        panelScrim?.let { s ->
            s.animate().alpha(0f).setDuration(120).withEndAction {
                if (!sheetShowing) s.visibility = View.GONE
            }.start()
        }
        if (panelContainer.visibility == View.VISIBLE) {
            panelContainer.animate()
                .translationY((panelContainer.height * 0.12f)
                    .coerceAtLeast(dp(24).toFloat()))
                .setDuration(140)
                .withEndAction {
                    if (!sheetShowing) {
                        panelContainer.visibility = View.GONE
                        panelContainer.translationY = 0f
                    }
                }.start()
        } else {
            panelContainer.visibility = View.GONE
        }
    }

    /** P4.3 (N2 — tab bar sobre o conteúdo com o teclado aberto): a altura
     *  do painel era FIXA (62% do display cheio); com o IME aberto a root
     *  encolhe e o painel continuava grande — campos ficavam atrás do
     *  chrome. AGORA a altura é recalculada a partir da root ATUAL (IME
     *  incluído) e o painel ancora ACIMA do chrome flutuante — TUDO por
     *  LayoutParams (requestLayout), NUNCA re-parent/remova-view: o
     *  EditText focado nunca é destacado (regressão B-B não volta). */
    private fun updatePanelHeight() {
        if (!::rootLayout.isInitialized || !::panelContainer.isInitialized) return
        if (activePanel == PANEL_NONE ||
            panelContainer.visibility != View.VISIBLE) return
        val rootH = rootLayout.height
        if (rootH <= 0) return
        val topH = if (::topBar.isInitialized) topBar.height else 0
        val bottomH = if (::bottomBar.isInitialized) bottomBar.height else 0
        val params = panelContainer.layoutParams as? FrameLayout.LayoutParams
            ?: return
        val isLandscape = resources.configuration.orientation ==
                android.content.res.Configuration.ORIENTATION_LANDSCAPE
        // Painel ancora ACIMA do chrome (margem) — em qualquer orientação.
        val marginTarget = bottomH + dp(6)
        val marginChanged = params.bottomMargin != marginTarget
        if (isLandscape) {
            // Drawer lateral flutuante: margens 8dp (margem inferior livra
            // o drawer do chrome).
            if (marginChanged) {
                params.bottomMargin = marginTarget
                panelContainer.layoutParams = params  // requestLayout SEM detach
            }
        } else {
            // Sheet inferior: no máximo 62% da altura VISÍVEL (root ATUAL —
            // com o IME aberto a root encolhe e o painel encolhe junto) e
            // nunca atrás do topBar/chrome.
            val target = minOf((rootH * 0.62f).toInt(),
                maxOf(rootH - topH - bottomH - dp(12), dp(120)))
            if (params.height != target || marginChanged) {
                params.height = target
                params.bottomMargin = marginTarget
                panelContainer.layoutParams = params  // requestLayout SEM detach
            }
        }
        // Campo em edição sobe à vista quando o espaço muda (IME abriu).
        if (!::inspectorScroll.isInitialized) return
        val focus = currentFocus
        if (focus != null && focus.width > 0) {
            focus.post {
                val bounds = android.graphics.Rect(0, 0, focus.width, focus.height)
                focus.requestRectangleOnScreen(bounds, true)
            }
        }
    }

    // --- P4.3 (N1): preview de áudio — TOGGLE com stop automático ----------

    /** 1º toque toca; 2º toque no MESMO asset para. Fonte de verdade: o
     *  mixer NATIVO (a voice pode ter terminado sozinha). */
    internal fun toggleAudioPreview(name: String): Boolean {
        if (handle == 0L) return false
        return if (EditorJni.nativeEditorAudioPreviewPlaying(handle)) {
            EditorJni.nativeEditorAudioPreviewStop(handle)
            false
        } else {
            EditorJni.nativeEditorAudioPreview(handle, name)
        }
    }

    /** Stop do preview (fechar painel / mudar categoria / importar /
     *  Play) — idempotente, nunca toca nas vozes de jogo. */
    internal fun stopAudioPreview() {
        if (handle != 0L) {
            EditorJni.nativeEditorAudioPreviewStop(handle)
        }
    }

    internal fun refreshPanel() {
        when (activePanel) {
            PANEL_HIERARCHY -> refreshHierarchy()
            PANEL_INSPECTOR -> refreshInspector()
            PANEL_ASSETS -> refreshAssets()
            PANEL_SCRIPTS -> refreshScripts()
            PANEL_ANIM -> refreshAnim()
            PANEL_TICKS -> refreshTicks()
        }
    }

    internal fun selectEntity(packed: Long) {
        // P4.1 (T1/D1 — CAUSA RAIZ): a seleção C++ é a FONTE do gizmo,
        // do hit-test e do render — este caminho (hierarquia/menus) só
        // atualizava a var Kotlin, e o gizmo ficava na entidade velha.
        // Fonte ÚNICA: o documento. Erro (entidade obsoleta) é mostrado,
        // nunca calado.
        if (handle != 0L &&
            !EditorJni.nativeEditorSelect(handle, packed)) {
            toastErr(lastErrorText())
        }
        selection = packed
        refreshHierarchy()
        refreshInspector()
    }

    internal var selection: Long = 0L

    internal fun refreshHierarchy() {
        val tsv = EditorJni.nativeEditorHierarchy(handle) ?: return
        hierarchyAdapter.reload(tsv)
        if (selection != 0L) {
            hierarchyAdapter.markSelected(selection)
        }
        hierarchyAdapter.notifyDataSetChanged()
        // P4.5 (§2): empty state da hierarquia (card curvo) ↔ lista.
        if (::hierarchyList.isInitialized) {
            (hierarchyList.tag as? View)?.visibility =
                if (hierarchyAdapter.count == 0) View.VISIBLE else View.GONE
        }
    }

    /**
     * P4.2 (B-B): dispatcher do sync do Inspector — DIFERENCIAL.
     * Mesma estrutura (mesma seleção, mesmos componentes/campos/kinds) →
     * atualiza os valores IN-PLACE (nenhuma view recriada; view com foco
     * NUNCA é tocada — o IME sobrevive). Estrutura mudou → rebuild completo.
     */
    internal fun refreshInspector() {
        if (handle == 0L) {
            inspectorScroll.removeAllViews()
            inspectorKey = null
            inspectorContent = null
            return
        }
        val key = inspectorStructureKey()
        val attached = inspectorContent?.parent === inspectorScroll
        if (key != null && key == inspectorKey && attached) {
            updateInspectorValuesInPlace()
            return
        }
        rebuildInspector(key)
    }

    /** Assinatura da ESTRUTURA do painel (seleção + componentes + campos
     *  + kinds) — barata (TSV nativos, já carregados pelo rebuild). */
    private fun inspectorStructureKey(): String? {
        if (handle == 0L) return null
        if (selection == 0L) return "none"
        val sb = StringBuilder()
        sb.append(selection).append('|')
        val componentsTsv =
            EditorJni.nativeEditorEntityComponents(handle, selection)
                ?: return null
        for (line in componentsTsv.lines().filter { it.isNotBlank() }) {
            val parts = line.split('\t')
            if (parts.size < 2) continue
            sb.append(parts[0]).append(',')
            val fieldsTsv =
                EditorJni.nativeEditorComponentFields(handle, selection, parts[0])
            if (fieldsTsv != null) {
                for (fline in fieldsTsv.lines().filter { it.isNotBlank() }) {
                    val fp = fline.split('\t')
                    if (fp.size < 3) continue
                    sb.append(fp[0]).append(':')
                        .append(fp.getOrElse(3) { "text" }).append(',')
                }
            }
        }
        return sb.toString()
    }

    internal fun refreshAll() {
        refreshHierarchySafe()
        if (::brand.isInitialized && handle != 0L) {
            val name = EditorJni.nativeEditorProjectName(handle) ?: ""
            // Marca carrega o projeto: identidade + contexto na MESMA linha
            // (evolução P0-4 — sem botão gigante de projeto).
            brand.text = if (name.isEmpty()) "G.ONI" else "G.ONI · $name"
        }
    }

    internal fun refreshHierarchySafe() {
        if (::hierarchyAdapter.isInitialized && activePanel == PANEL_HIERARCHY) {
            refreshHierarchy()
        }
    }

    // --- helpers de UI ----------------------------------------------------------

    /** Nome de exibição de componente: "eng::editor::SpriteData" → "Sprite"
     *  (P0-6: cabeçalhos legíveis; a CHAMADA de API continua com o nome cru). */
    internal fun prettyComponent(raw: String): String {
        var name = raw.substringAfterLast(':')
        if (name.endsWith("Data")) name = name.removeSuffix("Data")
        if (name.endsWith("Component")) name = name.removeSuffix("Component")
        return name
    }

    /** Rótulo de campo: último segmento do path; grupo de cor → rótulo base. */
    internal fun prettyFieldLabel(path: String): String {
        val leaf = path.substringBefore(',').substringAfterLast('.')
        return leaf.replaceFirstChar { it.uppercase() }
    }

    /** Hex "#RRGGBB[AA]" → ARGB int (ou null quando inválido). */
    internal fun parseHexColor(hex: String): Int? {
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
    internal fun setFieldQuiet(component: String, path: String, value: String) {
        val ok = EditorJni.nativeEditorSetComponentField(
            handle, selection, component, path, value
        )
        if (!ok) toastErr(lastErrorText())
    }

    internal fun fmtFloat(v: Float): String =
        if (v == v.toLong().toFloat() && kotlin.math.abs(v) < 1e6f) {
            v.toLong().toString()
        } else {
            String.format("%.3f", v)
        }

    internal fun currentEntityName(packed: Long): String {
        val tsv = EditorJni.nativeEditorHierarchy(handle) ?: return ""
        for (line in tsv.lines()) {
            val parts = line.split('\t')
            if (parts.size >= 3 && parts[2].toLongOrNull() == packed) {
                return parts[1]
            }
        }
        return ""
    }

    // --- P4.5: toasts pill (§2) com severidade -----------------------------------

    internal fun toast(text: String) = OniToast.show(this, text, Oni.TOAST_INFO)
    internal fun toastOk(text: String) = OniToast.show(this, text, Oni.TOAST_OK)
    internal fun toastErr(text: String) = OniToast.show(this, text, Oni.TOAST_ERR)

    /** Input 1 campo (padrão dos "Novo…" — agora OniDialog). */
    internal fun inputDialog(
        title: String, initial: String, onOk: (String) -> Unit
    ) {
        OniDialog.input(this, title, initial, onOk = onOk)
    }

    internal fun lastErrorText(): String =
        EditorJni.nativeEditorLastError(handle) ?: "operação falhou"

    // --- play/stop (§8.7) ------------------------------------------------------------

    internal fun togglePlay() {
        if (EditorJni.nativeEditorIsPlaying(handle)) {
            EditorJni.nativeEditorStop(handle)
            btnPlay.text = "▶"
            btnPlay.setTextColor(Oni.ON_ACCENT)
            btnPlay.background = Oni.ripple(this, Oni.ACCENT, Oni.R_BTN)
            playHud.visibility = View.GONE  // P4.1: HUD some com o Play
            applyGameModeChrome(false)
            // P4.2 (T5): Stop volta ao editor com SELEÇÃO e CÂMERA intactas.
            toast("STOP — seleção e câmera intactas")
        } else {
            if (EditorJni.nativeEditorPlay(handle)) {
                btnPlay.text = "■"
                btnPlay.setTextColor(Oni.BG)
                btnPlay.background = Oni.ripple(this, Oni.DANGER, Oni.R_BTN)
                applyGameModeChrome(true)
                // P4.1 (T2/D5): o que antes era silêncio agora é texto
                // IMEDIATO — compilação falhou? faults? HUD + toast.
                updatePlayHud()
                updateGameHudStatus()
                val stats = EditorJni.nativeEditorScriptStats(handle)
                if (stats != null) {
                    val p = stats.split('\t')
                    val failed = p.getOrNull(2)?.toIntOrNull() ?: 0
                    val found = p.getOrNull(0)?.toIntOrNull() ?: 0
                    if (found > 0 && failed > 0) {
                        val err = p.getOrNull(6)?.takeIf { it.isNotBlank() }
                            ?: "erro desconhecido"
                        toastErr("SCRIPT COM ERRO: $err")
                    }
                }
                // P4.1 (T3/D6): sem som → o autor SABE na hora (e o porquê).
                val audio = EditorJni.nativeEditorAudioStatus(handle)
                if (audio != null && audio.startsWith("null")) {
                    toastErr("ÁUDIO: ${audio.removePrefix("null:")}")
                }
            } else {
                toastErr(lastErrorText())
            }
        }
        refreshPanel()
    }

    /** P4.2 (T5 — Modo Jogo G1): chrome do editor some/volta. Em jogo:
     *  fullscreen (topBar/bottomBar/painéis fora), HUD de STOP/PAUSE
     *  visível; painel aberto fecha. No Stop, tudo volta. */
    private fun applyGameModeChrome(playing: Boolean) {
        gameMode = playing
        topBar.visibility = if (playing) View.GONE else View.VISIBLE
        bottomBar.visibility = if (playing) View.GONE else View.VISIBLE
        playHud.visibility = View.GONE
        gameHudBar?.visibility = if (playing) View.VISIBLE else View.GONE
        zoomCluster?.visibility = if (playing) View.GONE else View.VISIBLE
        undoCluster?.visibility = if (playing) View.GONE else View.VISIBLE
        if (playing && activePanel != PANEL_NONE) {
            togglePanel(activePanel)  // fecha o painel (toggle → NONE)
        }
        if (!playing) {
            fpsShown = 0f; fpsFrames = 0; fpsAccum = 0f
        }
    }

    /** P4.2 (T5): PAUSE/CONTINUE do runtime (estado no DOCUMENTO). */
    private fun togglePauseGame() {
        val paused = !EditorJni.nativeEditorIsPaused(handle)
        EditorJni.nativeEditorSetPaused(handle, paused)
        btnPauseGame?.text = if (paused) "▶" else "⏸"
        updateGameHudStatus()
        toast(if (paused) "PAUSE — runtime congelado" else "Play — runtime rodando")
    }

    /** Linha de estado do HUD do Modo Jogo (backend de áudio + fps + PAUSE). */
    private fun updateGameHudStatus() {
        val audio = EditorJni.nativeEditorAudioStatus(handle) ?: "off"
        val fps = if (fpsShown > 0f) String.format(" · %.0f fps", fpsShown) else ""
        val pause = if (EditorJni.nativeEditorIsPaused(handle)) " · PAUSE" else ""
        gameHudStatus?.text = "Áudio: $audio$fps$pause"
    }

    /** P4.1: HUD do Play — "Scripts: N inst · T ticks · F faults" + áudio. */
    private fun updatePlayHud() {
        val stats = EditorJni.nativeEditorScriptStats(handle)
        val audio = EditorJni.nativeEditorAudioStatus(handle) ?: "off"
        val p = stats?.split('\t')
        val scriptsLine = if (p != null && p.size >= 8) {
            val found = p[0]
            val failed = p[2]
            val instances = p[3]
            val ticks = p[4]
            val faults = p[5]
            if (found == "0" && failed == "0") "Scripts: nenhum na cena"
            else "Scripts: $instances inst · $ticks ticks · $faults faults" +
                (if (failed != "0") " · $failed COM ERRO" else "")
        } else {
            "Scripts: —"
        }
        playHud.text = "$scriptsLine\nÁudio: $audio"
    }

    // --- gestos do viewport (§8.6/§8.8 — eventos do EDITOR, não do jogo) ------------------

    /**
     * Em PLAY, os toques do viewport vão ao INPUT DO JOGO (§6.4). P4.2
     * (T5 — Modo Jogo): a rota é do JOGO INTEIRO durante o Play — o
     * chrome some, então não existe gesto de editor a preservar.
     */
    private fun gameWantsTouch(): Boolean =
        handle != 0L && EditorJni.nativeEditorIsPlaying(handle)

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
            // Bugs C-5/C-6 da auditoria final: em Play, os eventos BRUTOS
            // vão ao input do jogo com fases e pointer IDs REAIS. P4.2 (T5):
            // em Play a rota é do JOGO inteiro.
            if (gameWantsTouch()) {
                dispatchGameTouch(event)
                true
            } else {
                handleGizmoTouch(event)  // P1: raw events p/ drag de gizmo
                // P4.2 (B-D): durante o drag do gizmo os detectores NÃO
                // veem o evento — um 2º dedo não vira pinch.
                if (!gizmoDragging) {
                    scaleDetector.onTouchEvent(event)
                    tapDetector.onTouchEvent(event)
                }
                true
            }
        }
    }

    /**
     * Drag do GIZMO (P1.3–P1.5) — eventos CRUS: o GestureDetector entrega
     * apenas DELTAS de scroll; rotação/escala precisam da posição
     * ABSOLUTA do pointer. Handles têm PRIORIDADE sobre o corpo da
     * entidade (P1.6).
     */
    private var gizmoDragging = false
    /// P4.2 (B-D): pointer ID DONO do drag.
    private var gizmoPointerId = -1

    private fun handleGizmoTouch(event: MotionEvent) {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                gizmoDragging = false
                gizmoPointerId = event.getPointerId(0)
                if (editorTool != 0 && selection != 0L) {
                    val handleId = EditorJni.nativeEditorGizmoDragBegin(
                        handle, event.x, event.y
                    )
                    gizmoDragging = handleId != 0
                }
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                // Dedo EXTRA nunca rouba o drag (o dono continua o mesmo).
                if (!gizmoDragging) {
                    gizmoPointerId = event.getPointerId(event.actionIndex)
                }
            }
            MotionEvent.ACTION_MOVE -> if (gizmoDragging) {
                // Sempre o POINTER DO DRAG (não o índice 0).
                val idx = event.findPointerIndex(gizmoPointerId)
                if (idx < 0) return  // dono sumiu: UP/CANCEL chega em seguida
                if (!EditorJni.nativeEditorGizmoDragTo(
                        handle, event.getX(idx), event.getY(idx)
                    )
                ) {
                    gizmoDragging = false  // erro: entidade morreu no drag
                }
            }
            MotionEvent.ACTION_POINTER_UP -> {
                // O DONO do drag levantou → fim honesto.
                if (gizmoDragging &&
                    event.getPointerId(event.actionIndex) == gizmoPointerId
                ) {
                    EditorJni.nativeEditorGizmoDragEnd(handle)
                    gizmoDragging = false
                    gizmoPointerId = -1
                    refreshInspectorIfOpen()  // P1.9: campos finais do drag
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                if (gizmoDragging) {
                    EditorJni.nativeEditorGizmoDragEnd(handle)
                    refreshInspectorIfOpen()  // P1.9: campos finais do drag
                }
                gizmoDragging = false
                gizmoPointerId = -1
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
        // P3.5 (T2): o DISPATCH em si é um sub-passo — prova que o
        // framework entregou a surface à Activity (antes do JNI).
        EditorJni.nativeStartupMark("SURFACE_DISPATCH", "begin",
            "framework entregou a surface")
        EditorJni.nativeWatchdogHeartbeat()
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
        // P3.5 (T2): primeiro doFrame = o Choreographer voltou a entregar
        // frames — o traversal do Android começou (a surface vem em
        // seguida). Também é o heartbeat natural do watchdog (60 Hz).
        if (!firstTraversalMarked) {
            firstTraversalMarked = true
            EditorJni.nativeStartupMark("FIRST_TRAVERSAL", "begin",
                "primeiro doFrame do choreographer")
        }
        EditorJni.nativeWatchdogHeartbeat()
        if (handle != 0L && surfaceReady) {
            val delta = if (lastFrameNanos == 0L) 0f
                        else (nanos - lastFrameNanos) / 1e9f
            lastFrameNanos = nanos
            EditorJni.nativeEditorRenderFrame(handle, delta.coerceIn(0f, 0.1f))
            // P4.1 (T2/D5): HUD do Play ao vivo (a cada ~0.5 s) — ticks
            // crescendo = script RODANDO; faults subindo = binding falhou.
            if (EditorJni.nativeEditorIsPlaying(handle)) {
                hudFrameCounter++
                // P4.2 (T5): fps barato (média por janela de 30 frames).
                if (delta > 0f) {
                    fpsAccum += delta
                    fpsFrames++
                }
                if (hudFrameCounter % 30L == 0L) {
                    if (fpsAccum > 0f && fpsFrames > 0) {
                        fpsShown = fpsFrames / fpsAccum
                    }
                    fpsAccum = 0f
                    fpsFrames = 0
                    updatePlayHud()
                    // No Modo Jogo o playHud fica FORA (HUD novo mostra
                    // áudio/fps).
                    if (!gameMode) {
                        playHud.visibility = View.VISIBLE
                    }
                    updateGameHudStatus()
                }
            }
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
                updateUndoRedo()  // P4.5: FABs vivem com o histórico
            }
        }
        choreographer?.postFrameCallback(this)
    }

    // --- thumbnails (P0-6): decode com inSampleSize + cache em memória ---------

    internal val thumbCache = HashMap<String, android.graphics.Bitmap>()

    /** Bitmap reduzido do asset do PROJETO ATUAL (textures) — null se não
     *  é imagem decodificável. Cache por nome. */
    internal fun thumbnailOf(category: String, name: String): android.graphics.Bitmap? {
        val project = EditorJni.nativeEditorProjectName(handle) ?: return null
        return thumbnailOfIn(project, category, name)
    }

    /** Variante para o project switcher (thumbnail de QUALQUER projeto). */
    internal fun thumbnailOfIn(
        project: String, category: String, name: String
    ): android.graphics.Bitmap? {
        val key = "$project/$category/$name"
        thumbCache[key]?.let { return it }
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

    // --- adapters ---------------------------------------------------------------------------

    /** Linha da hierarquia (TSV depth\tname\tpacked). */
    internal data class HierarchyRow(val depth: Int, val name: String, val packed: Long)

    internal inner class HierarchyAdapter : ArrayAdapter<HierarchyRow>(
        this@EditorActivity, R.layout.oni_list_item
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
            val row = rows[position]
            // P4.5 (§2): row curva sem divisor, tom+espaço, pressed overlay;
            // seleção = tint acento + texto acento.
            val view = LinearLayout(this@EditorActivity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                minimumHeight = dp(48)
                setPadding(dp(12) + row.depth * dp(12), dp(6), dp(12), dp(6))
                if (row.packed == selection) {
                    background = Oni.ripplePill(this@EditorActivity, 0x2E8AB4F8.toInt())
                } else {
                    background = Oni.rippleOnly(this@EditorActivity)
                }
            }
            view.addView(TextView(this@EditorActivity).apply {
                // Densidade de editor: tipo à frente + indentação por
                // profundidade (evolução P0-4 — hierarquia LEGÍVEL).
                text = if (row.packed == selection) "▶ ${row.name}" else row.name
                setTextColor(if (row.packed == selection) Oni.ACCENT else Oni.TEXT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
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
    internal data class AssetEntry(
        val name: String, val id: String, val registered: Boolean, val path: String
    )

    internal inner class AssetAdapter : ArrayAdapter<AssetEntry>(
        this@EditorActivity, R.layout.oni_list_item
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
            val category = assetCategoryName
            // Linha densa com THUMBNAIL REAL (P0-6): imagens mostram o
            // conteúdo; demais tipos mostram o nome + id/metadata.
            val row = LinearLayout(this@EditorActivity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                minimumHeight = dp(56)
                setPadding(dp(12), dp(6), dp(12), dp(6))
                background = Oni.rippleOnly(this@EditorActivity)
            }
            if (category == "textures") {
                val thumb = thumbnailOf("textures", entry.name)
                if (thumb != null) {
                    row.addView(
                        ImageView(this@EditorActivity).apply {
                            setImageBitmap(thumb)
                            scaleType = android.widget.ImageView.ScaleType.FIT_CENTER
                            background =
                                Oni.rounded(this@EditorActivity, Oni.RAISED, Oni.R_THUMB)
                            clipToOutline = true
                        },
                        LinearLayout.LayoutParams(dp(44), dp(44))
                    )
                    row.addView(
                        Space(this@EditorActivity),
                        LinearLayout.LayoutParams(dp(10), dp(1))
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
                setTextColor(if (entry.registered) Oni.TEXT else Oni.TEXT_DIM)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                typeface = Typeface.DEFAULT_BOLD
            })
            texts.addView(TextView(this@EditorActivity).apply {
                text = when {
                    imageInfo != null -> imageInfo
                    entry.registered -> "id ${entry.id.take(8)}…"
                    else -> "(não catalogado)"
                }
                setTextColor(Oni.TEXT_DIM)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                typeface = Typeface.MONOSPACE
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

    // --- startup do projeto (P3 §0 — política no C++, erro controlado aqui) ---

    private fun ensureProjectOnFirstRun() {
        val opened = EditorJni.nativeEditorEnsureProject(handle)
        if (opened == null) {
            toastErr(lastErrorText())
        }
    }

    // --- SAF (P2 §17): armazenamento do usuário COM permissão persistente ------
    //
    // Modelo documentado: os PROJETOS vivem no workspace privado
    // (filesDir/projects — sem permissões, nunca "assume acesso
    // irrestrito", paths sempre relativos — RootedFileSystem na fronteira).
    // O SAF dá ao AUTOR o canal de intercâmbio (zip ida e volta).

    internal val safPrefs by lazy {
        getSharedPreferences("goni_saf", Context.MODE_PRIVATE)
    }

    // Requests de SAF (dispatch no onActivityResult ÚNICO, abaixo).
    private val reqSafFolder = 4101
    private val reqSafExport = 4102
    private val reqSafImport = 4103
    private val reqSafDiag = 4104

    /** Dispatch SAF (chamado pelo onActivityResult ÚNICO da Activity). */
    private fun handleSafResult(requestCode: Int, resultCode: Int, uri: Uri?) {
        if (resultCode != RESULT_OK || uri == null) return
        when (requestCode) {
            reqSafFolder -> {
                // Permissão PERSISTENTE: o grant sobrevive a restarts —
                // o mecanismo do Android (não um path absoluto salvo).
                try {
                    contentResolver.takePersistableUriPermission(
                        uri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION or
                            Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    )
                    safPrefs.edit().putString("export_tree", uri.toString()).apply()
                    toastOk("Pasta de exportação autorizada (permissão persistente)")
                } catch (e: SecurityException) {
                    toastErr("Sem permissão persistível: ${e.message}")
                }
            }
            reqSafExport -> writeProjectZipTo(uri)
            reqSafImport -> importProjectZipFrom(uri)
            reqSafDiag -> writeDiagnosticsZipTo(uri)
        }
    }

    internal fun pickImportFile() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            // P4.2 (B-E): import de ÁUDIO filtra WAV no picker — o conteúdo
            // é revalidado no C++ (EditorDocument::importAsset), mas o
            // picker certo evita o erro ANTES de copiar o arquivo.
            if (assetCategoryName == "audio") {
                putExtra(
                    Intent.EXTRA_MIME_TYPES,
                    arrayOf("audio/wav", "audio/x-wav", "audio/wave", "audio/vnd.wave")
                )
            }
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
        } ?: run { toastErr("Não foi possível ler o arquivo"); return }

        val category = assetCategoryName
        inputDialog("Nome do asset", dest.nameWithoutExtension) { name ->
            // Path relativo ao ROOT do projeto (workspace/projects/<p>/.import_tmp/x)
            val rel = ".import_tmp/${dest.name}"
            if (EditorJni.nativeEditorAssetImport(handle, rel, category, name)) {
                toastOk("Importado em $category")
                // P4.3 (N1): importou — contexto do preview mudou (lista de
                // assets mudou); voice do preview morre.
                stopAudioPreview()
                refreshAssets()
            } else {
                toastErr(lastErrorText())
            }
        }
    }

    internal companion object {
        internal const val PANEL_NONE = 0
        internal const val PANEL_HIERARCHY = 1
        internal const val PANEL_INSPECTOR = 2
        internal const val PANEL_ASSETS = 3
        internal const val PANEL_SCRIPTS = 4
        internal const val PANEL_ANIM = 5
        internal const val PANEL_TICKS = 6
        internal const val REQUEST_IMPORT = 4101
    }
}
