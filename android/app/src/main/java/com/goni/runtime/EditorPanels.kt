package com.goni.runtime

import android.graphics.Typeface
import android.text.InputType
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.ScrollView
import android.widget.Space
import android.widget.Switch
import android.widget.TextView

/**
 * P4.5 — CONTEÚDO dos painéis (hierarquia/inspector/assets/scripts/
 * animação/ticks) como extensões do EditorActivity: o comportamento é o
 * mesmo da P4.1–P4.3 (B-B: sync diferencial do Inspector; N1: stop do
 * preview; N2: painel adaptativo) — só a SUPERFÍCIE muda (OniUi).
 */
private const val TAG = "EditorPanels"

// --- sheet Ticks & Camadas (P4.3 — Bloco 2; ADR-051 autorável) -----------------

internal data class LayerRow(
    val name: String,
    val timeScale: Float,
    val update: Boolean,
    val physics: Boolean,
    val render: Boolean
)

fun EditorActivity.buildTicksPanel() {
    val act = this
    val bar = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        setPadding(dp(12), 0, dp(12), dp(4))
    }
    bar.addView(
        Oni.chip(this, "+ Camada", active = true, textSizeSp = 12f).also {
            it.setOnClickListener { addLayerDialog() }
        },
        LinearLayout.LayoutParams(0, dp(48), 1f)
    )
    // Timestep FIXO da física (s) — configura o PhysicsTick do Play.
    // Aplica no IME_ACTION_DONE (campo vivo — zero UI morta).
    bar.addView(
        TextView(this).apply {
            text = "Dt física (s):"
            setTextColor(Oni.TEXT_DIM)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            setPadding(dp(8), 0, dp(4), 0)
            gravity = Gravity.CENTER_VERTICAL
        },
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, dp(48)
        )
    )
    val physicsDtField = Oni.field(this, mono = true).apply {
        setSingleLine()
        inputType = InputType.TYPE_CLASS_NUMBER or
            InputType.TYPE_NUMBER_FLAG_DECIMAL
        setText(fmtFloat(EditorJni.nativeEditorPhysicsDt(handle)))
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
        imeOptions = android.view.inputmethod.EditorInfo.IME_ACTION_DONE
        setOnEditorActionListener { _, _, _ ->
            applyPhysicsDt()
            true
        }
    }
    bar.addView(physicsDtField, LinearLayout.LayoutParams(dp(84), dp(48)))
    val tickView = ListView(this).apply {
        divider = null
        dividerHeight = 0
        selector = android.graphics.drawable.ColorDrawable(0)
        setPadding(dp(8), dp(4), dp(8), dp(8))
        clipToPadding = false
        onItemClickListener =
            AdapterView.OnItemClickListener { _, _, position, _ ->
                tickLayerRows.getOrNull(position)?.let { layerEditDialog(it) }
            }
    }
    ticksList = tickView
    tickView.tag = physicsDtField  // applyPhysicsDt lê o campo vivo
    panelContainer.addView(
        bar,
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        )
    )
    panelContainer.addView(
        tickView,
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT
        )
    )
}

fun EditorActivity.refreshTicks() {
    val act = this
    val list = ticksList ?: return
    if (handle == 0L) return
    val tsv = EditorJni.nativeEditorLayerList(handle) ?: return
    tickLayerRows.clear()
    val display = mutableListOf<Pair<String, String>>()  // título / sub
    for (line in tsv.lines().filter { it.isNotBlank() }) {
        val p = line.split('\t')
        val name = p.getOrNull(0) ?: continue
        val ts = p.getOrNull(1)?.toFloatOrNull() ?: 1f
        val u = p.getOrNull(2) == "1"
        val f = p.getOrNull(3) == "1"
        val r = p.getOrNull(4) == "1"
        tickLayerRows.add(LayerRow(name, ts, u, f, r))
        val flags = listOfNotNull(
            if (u) "update" else null,
            if (f) "física" else null,
            if (r) "render" else null
        ).joinToString(" · ")
        display.add(name to "timeScale ${fmtFloat(ts)} · $flags")
    }
    list.adapter = object : ArrayAdapter<Pair<String, String>>(
        this, R.layout.oni_list_item, display
    ) {
        override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
            val (title, sub) = display[position]
            val row = Oni.listRow(act, twoLine = true)
            val col = LinearLayout(act).apply {
                orientation = LinearLayout.VERTICAL
            }
            col.addView(TextView(act).apply {
                text = title
                setTextColor(Oni.TEXT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                typeface = Typeface.DEFAULT_BOLD
            })
            col.addView(TextView(act).apply {
                text = sub
                setTextColor(Oni.TEXT_DIM)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                typeface = Typeface.MONOSPACE
            })
            row.addView(col, LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            row.addView(TextView(act).apply {
                text = "›"
                setTextColor(Oni.TEXT_DIM)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            })
            return row
        }
    }
}

fun EditorActivity.applyPhysicsDt() {
    val act = this
    val field = (ticksList?.tag as? EditText) ?: return
    val v = field.text.toString().toFloatOrNull()
    if (v == null) {
        toastErr("Valor inválido (ex.: 0.0167)")
        return
    }
    if (!EditorJni.nativeEditorPhysicsSetDt(handle, v)) {
        toastErr(lastErrorText())
    } else {
        toastOk("Timestep da física: ${fmtFloat(v)} s")
    }
}

// --- sheet Hierarquia (§8.3) ----------------------------------------------------

fun EditorActivity.buildHierarchyPanel() {
    val act = this
    val bar = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        setPadding(dp(12), 0, dp(12), dp(4))
    }
    bar.addView(
        Oni.chip(this, "+ Entidade", active = true, textSizeSp = 12f).also {
            it.setOnClickListener { createEntityDialog() }
        },
        LinearLayout.LayoutParams(0, dp(48), 1f)
    )
    bar.addView(
        Oni.chip(this, "+ Sprite", textSizeSp = 12f).also {
            it.setOnClickListener { addSpriteDialog() }
        },
        LinearLayout.LayoutParams(0, dp(48), 1f)
    )
    hierarchyList = ListView(this).apply {
        adapter = hierarchyAdapter
        divider = null
        dividerHeight = 0
        selector = android.graphics.drawable.ColorDrawable(0)
        setPadding(dp(8), dp(4), dp(8), dp(8))
        clipToPadding = false
        onItemClickListener =
            AdapterView.OnItemClickListener { _, _, _, _ ->
                val packed = hierarchyAdapter.selectedAt()
                if (packed != 0L) {
                    // P1.0 BUG FIX: a seleção via hierarquia precisa
                    // chegar ao DOCUMENTO (borda no viewport + alvo do
                    // gizmo) — antes só o Kotlin sabia.
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
    // P4.5 (§2): empty state — card curvo com ícone + dica + ação.
    val empty = Oni.emptyState(
        this, "◇",
        "Nenhuma entidade na cena.\nCrie a primeira para começar.",
        "+ Entidade"
    ) { createEntityDialog() }
    val host = FrameLayout(this)
    host.addView(hierarchyList, FrameLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.MATCH_PARENT))
    host.addView(empty, FrameLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT,
        Gravity.CENTER).apply { setMargins(dp(24), dp(8), dp(24), 0) })
    hierarchyList.tag = empty  // refreshHierarchy alterna a visibilidade
    panelContainer.addView(
        bar,
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        )
    )
    panelContainer.addView(
        host,
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT
        )
    )
}

// --- sheet Inspector (§8.4) -------------------------------------------------------

fun EditorActivity.buildInspectorPanel() {
    val act = this
    panelContainer.addView(
        inspectorScroll,
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT
        )
    )
}

/**
 * P4.2 (B-B): valores in-place — diff antes de setText; FOCADO nunca é
 * tocado (a fonte daquele campo é o teclado até o DONE — P1.9).
 */
fun EditorActivity.updateInspectorValuesInPlace() {
    val act = this
    val sel = selection
    if (sel == 0L) return
    inspectorNameField?.let { f ->
        if (!f.hasFocus()) {
            val name = currentEntityName(sel)
            if (f.text.toString() != name) f.setText(name)
        }
    }
    updateTransformFieldsLive()
    val componentsTsv =
        EditorJni.nativeEditorEntityComponents(handle, sel) ?: return
    for (line in componentsTsv.lines().filter { it.isNotBlank() }) {
        val parts = line.split('\t')
        if (parts.size < 2) continue
        val component = parts[0]
        val fieldsTsv =
            EditorJni.nativeEditorComponentFields(handle, sel, component)
                ?: continue
        for (fline in fieldsTsv.lines().filter { it.isNotBlank() }) {
            val fp = fline.split('\t')
            if (fp.size < 3) continue
            val path = fp[0]
            val value = fp[2]
            inspectorTextFields["$component\u0001$path"]?.let { f ->
                if (!f.hasFocus() && f.text.toString() != value) {
                    f.setText(value)
                }
            }
            inspectorValueViews["$component\u0001$path"]?.let { (v, empty) ->
                val shown = value.ifEmpty { empty }
                if (v.text.toString() != shown) v.text = shown
            }
            // P4.6 (Bloco 1): rows de bitfield re-estilizam in-place.
            inspectorBitfieldRows["$component\u0001$path"]?.let { row ->
                (row.tag as? BitfieldHolder)?.let { holder ->
                    val newBits = value.toLongOrNull()
                    if (newBits != null && newBits != holder.value) {
                        holder.value = newBits
                        styleBitfieldChips(holder)
                    }
                }
            }
        }
    }
}

/** Rebuild COMPLETO (estrutura mudou / primeira abertura). Registra
 *  as views atualizáveis nos mapas do sync diferencial. */
fun EditorActivity.rebuildInspector(key: String?) {
    val act = this
    inspectorTextFields.clear()
    inspectorValueViews.clear()
    inspectorNameField = null
    inspectorKey = key
    val content = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(12), dp(4), dp(12), dp(16))
    }
    if (selection == 0L) {
        // P4.5 (§2): empty state curvo com dica + ação (era label solto).
        content.addView(Oni.emptyState(
            this, "◇",
            "Nenhuma entidade selecionada.\nToque no viewport ou na hierarquia.",
            "+ Sprite"
        ) { addSpriteDialog() }.apply {
            setPadding(dp(8), dp(12), dp(8), dp(8))
        })
        inspectorScroll.removeAllViews()
        inspectorScroll.addView(content)
        inspectorContent = content
        return
    }

    // Cabeçalho: nome da entidade (editável).
    val nameField = Oni.field(this).apply {
        setSingleLine()
        setText(currentEntityName(selection))
        hint = "Nome"
        imeOptions = android.view.inputmethod.EditorInfo.IME_ACTION_DONE
        setOnEditorActionListener { _, actionId, _ ->
            if (actionId == android.view.inputmethod.EditorInfo.IME_ACTION_DONE) {
                val ok = EditorJni.nativeEditorRenameEntity(handle, selection, text.toString())
                if (!ok) toastErr(lastErrorText())
                refreshHierarchy()
                true
            } else false
        }
    }
    inspectorNameField = nameField
    content.addView(nameField, LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT, dp(48)))

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
                    Oni.button(this, "Remover ${prettyComponent(component)}",
                        kind = Oni.BTN_DANGER, textSizeSp = 13f).also {
                        it.setOnClickListener {
                            val ok = EditorJni.nativeEditorRemoveComponent(
                                handle, selection, component)
                            if (!ok) toastErr(lastErrorText())
                            refreshPanel()
                        }
                    },
                    LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, dp(48)
                    ).apply { topMargin = dp(6) }
                )
            }
        }
    }
    content.addView(
        Oni.chip(this, "+ Adicionar componente", active = true, textSizeSp = 13f).also {
            it.setOnClickListener { addComponentDialog() }
        },
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dp(48)
        ).apply { topMargin = dp(10) }
    )

    // Ações rápidas da seleção (P1.7/P1.8 — um toque, sem long-press).
    val quick = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        setPadding(0, dp(8), 0, 0)
    }
    quick.addView(
        Oni.chip(this, "Duplicar", textSizeSp = 13f).also {
            it.setOnClickListener {
                val dup = EditorJni.nativeEditorDuplicateEntity(handle, selection)
                if (dup == 0L) toastErr(lastErrorText()) else selectEntity(dup)
            }
        },
        LinearLayout.LayoutParams(0, dp(48), 1f)
    )
    quick.addView(
        Space(this), LinearLayout.LayoutParams(dp(8), dp(1))
    )
    quick.addView(
        Oni.chip(this, "Apagar", textSizeSp = 13f).also {
            it.setTextColor(Oni.DANGER)
            it.setOnClickListener {
                if (EditorJni.nativeEditorDeleteEntity(handle, selection)) {
                    selection = 0L
                    refreshPanel()
                } else {
                    toastErr(lastErrorText())
                }
            }
        },
        LinearLayout.LayoutParams(0, dp(48), 1f)
    )
    content.addView(quick)

    inspectorScroll.removeAllViews()
    inspectorScroll.addView(content)
    inspectorContent = content
}

/** Escreve o transform com os 9 CAMPOS vivos do painel (P1.9). */
fun EditorActivity.applyTransformFromFields() {
    val act = this
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
        toastErr(lastErrorText())
    }
    updateTransformFieldsLive()  // ecoa o que o documento aceitou
}

/** Rebuild do Inspector quando aberto (drag de gizmo terminou — P1.9). */
fun EditorActivity.refreshInspectorIfOpen() {
    val act = this
    if (activePanel == EditorActivity.PANEL_INSPECTOR && selection != 0L) {
        refreshInspector()
    }
}

/**
 * Live sync (P1.9): atualiza SO os campos de transform do painel
 * aberto — sem rebuild. Pula campos com FOCO (o usuário está
 * digitando; o teclado é a fonte daquele campo até o DONE).
 */
fun EditorActivity.updateTransformFieldsLive() {
    val act = this
    if (activePanel != EditorActivity.PANEL_INSPECTOR || selection == 0L) return
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

/** Linha TRS (3 campos curvos mono — §2). */
fun EditorActivity.addVec3Row(
    parent: LinearLayout, title: String, x: Float, y: Float, z: Float,
    apply: (FloatArray) -> Unit
) {
    val act = this
    parent.addView(labelView(title))
    val row = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        setPadding(0, 0, 0, dp(2))
    }
    val fields = mutableListOf<EditText>()
    if (collectTransformFields) transformFields.addAll(fields)
    for (value in listOf(x, y, z)) {
        val edit = Oni.field(this, mono = true).apply {
            inputType = InputType.TYPE_CLASS_NUMBER or
                InputType.TYPE_NUMBER_FLAG_SIGNED or
                InputType.TYPE_NUMBER_FLAG_DECIMAL
            setSingleLine()
            setText(fmtFloat(value))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
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
        if (fields.size > 1) {
            row.addView(Space(this), LinearLayout.LayoutParams(dp(6), dp(1)))
        }
        row.addView(
            edit,
            LinearLayout.LayoutParams(0, dp(48), 1f)
        )
    }
    parent.addView(row)
}

// --- sheet Assets (§8.5) ---------------------------------------------------------

/** Importar vs "Novo material…" (P3 §3): materiais são AUTORADOS,
 *  não importados — o botão da categoria reflete isso. */
fun EditorActivity.onImportButton() {
    val act = this
    val category = assetCategoryName
    if (category == "materials") {
        inputDialog("Nome do material", "NovoMaterial") { name ->
            if (EditorJni.nativeEditorMaterialCreate(handle, name)) {
                toastOk("Material '$name' criado")
                refreshAssets()
            } else toastErr(lastErrorText())
        }
        return
    }
    pickImportFile()
}

fun EditorActivity.buildAssetsPanel() {
    val act = this
    assetsRoot = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(8), 0, dp(8), 0)
    }
    val bar = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
    }
    // P4.5: categoria = chip pill que abre picker curvo (era dropdown default).
    assetCategoryChip = Oni.chip(this, "textures", mono = true, textSizeSp = 12f).also {
        it.setOnClickListener { showCategoryPicker() }
    }
    bar.addView(assetCategoryChip, LinearLayout.LayoutParams(0, dp(48), 1.4f))
    val btnImport = Oni.button(this, "Importar", kind = Oni.BTN_GHOST, textSizeSp = 13f)
    importButton = btnImport
    btnImport.setOnClickListener { onImportButton() }
    bar.addView(btnImport, LinearLayout.LayoutParams(0, dp(48), 1f))
    // Busca de assets (P0-6): filtra por nome dentro da categoria.
    assetSearch = Oni.field(this).apply {
        setSingleLine()
        hint = "Buscar…"
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
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
        divider = null
        dividerHeight = 0
        selector = android.graphics.drawable.ColorDrawable(0)
        clipToPadding = false
        onItemLongClickListener =
            AdapterView.OnItemLongClickListener { _, _, _, _ ->
                assetAdapter.selectedOrNull()?.let { assetMenuDialog(it) }
                true
            }
    }
    // P4.5 (§2): empty state do Assets (ação muda com a categoria).
    val empty = Oni.emptyState(
        this, "▢", "Nenhum asset nesta categoria.",
        "Importar"
    ) { onImportButton() }
    val root = assetsRoot!!
    root.tag = empty
    val listHost = FrameLayout(this)
    listHost.addView(assetList, FrameLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.MATCH_PARENT))
    listHost.addView(empty, FrameLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT,
        Gravity.CENTER).apply { setMargins(dp(24), dp(8), dp(24), 0) })
    root.addView(
        bar,
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        )
    )
    root.addView(
        assetSearch,
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dp(48)
        ).apply { topMargin = dp(4) }
    )
    root.addView(
        listHost,
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT
        )
    )
}

/** Picker curvo das categorias de assets (substitui o dropdown antigo). */
fun EditorActivity.showCategoryPicker() {
    val act = this
    val cats = (EditorJni.nativeEditorAssetCategories(handle) ?: "")
        .lines().filter { it.isNotBlank() }
    if (cats.isEmpty()) return
    val current = cats.indexOf(assetCategoryName)
    OniDialog.choice(this, "Categoria", cats, current) { which ->
        // P4.3 (N1): mudou a categoria — a lista muda de assets;
        // a voice do preview morre com o contexto da lista.
        stopAudioPreview()
        assetCategoryName = cats[which]
        refreshAssets()
    }
}

fun EditorActivity.refreshAssets() {
    val act = this
    if (handle == 0L) return
    val cats = EditorJni.nativeEditorAssetCategories(handle) ?: return
    val catList = cats.lines().filter { it.isNotBlank() }
    if (catList.isEmpty()) return
    if (assetCategoryName.isEmpty() || assetCategoryName !in catList) {
        assetCategoryName = catList[0]
    }
    assetCategoryChip?.text = assetCategoryName
    importButton?.text =
        if (assetCategoryName == "materials") "Novo…" else "Importar" 
    val tsv = EditorJni.nativeEditorAssetList(handle, assetCategoryName)
    // Busca (P0-6): filtro por nome, insensível a caixa.
    val filtered = (tsv ?: "").lines().filter {
        it.isNotBlank() && (assetQuery.isBlank() ||
            it.contains(assetQuery, ignoreCase = true))
    }
    assetAdapter.reload(filtered)
    assetAdapter.notifyDataSetChanged()
    // P4.5: empty state por categoria (lista vazia ↔ card).
    (assetsRoot?.tag as? View)?.visibility =
        if (filtered.isEmpty()) View.VISIBLE else View.GONE
}

// --- sheet Scripts NI-Script (evolução P0-7, ADR-053) -----------------------------

fun EditorActivity.buildScriptsPanel() {
    val act = this
    val bar = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        setPadding(dp(12), 0, dp(12), dp(4))
    }
    bar.addView(
        Oni.chip(this, "+ Novo script", active = true, textSizeSp = 12f).also {
            it.setOnClickListener { newScriptDialog() }
        },
        LinearLayout.LayoutParams(0, dp(48), 1f)
    )
    scriptsList = ListView(this).apply {
        divider = null
        dividerHeight = 0
        selector = android.graphics.drawable.ColorDrawable(0)
        setPadding(dp(8), dp(4), dp(8), dp(8))
        clipToPadding = false
        onItemClickListener =
            AdapterView.OnItemClickListener { _, _, position, _ ->
                val name = scriptNames.getOrNull(position)
                if (name != null) scriptEditorDialog(name)
            }
    }
    val empty = Oni.emptyState(
        this, "ƒ", "Nenhum script no projeto.\nCrie o primeiro .nis.",
        "+ Novo script"
    ) { newScriptDialog() }
    val sList = scriptsList!!
    sList.tag = empty
    val host = FrameLayout(this)
    host.addView(sList, FrameLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.MATCH_PARENT))
    host.addView(empty, FrameLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT,
        Gravity.CENTER).apply { setMargins(dp(24), dp(8), dp(24), 0) })
    panelContainer.addView(
        bar,
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        )
    )
    panelContainer.addView(
        host,
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT
        )
    )
}

fun EditorActivity.refreshScripts() {
    val act = this
    val list = scriptsList ?: return
    if (handle == 0L) return
    val tsv = EditorJni.nativeEditorScriptList(handle) ?: return
    scriptNames.clear()
    scriptNames.addAll(tsv.lines().filter { it.isNotBlank() })
    list.adapter = object : ArrayAdapter<String>(
        this, R.layout.oni_list_item, scriptNames.toList()
    ) {
        override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
            val name = scriptNames[position]
            val row = Oni.listRow(act)
            row.addView(TextView(act).apply {
                text = name
                setTextColor(Oni.TEXT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                typeface = Typeface.MONOSPACE
            }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            row.addView(TextView(act).apply {
                text = "›"
                setTextColor(Oni.TEXT_DIM)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            })
            return row
        }
    }
    (list.tag as? View)?.visibility =
        if (scriptNames.isEmpty()) View.VISIBLE else View.GONE
}

fun EditorActivity.newScriptDialog() {
    val act = this
    inputDialog("Nome do script", "Movimento") { name ->
        if (EditorJni.nativeEditorScriptCreate(handle, name)) {
            refreshScripts()
        } else {
            toastErr(lastErrorText())
        }
    }
}

// --- sheet ANIMAÇÃO (P2 §8) --------------------------------------------------------

fun EditorActivity.buildAnimPanel() {
    val act = this
    val bar = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        setPadding(dp(12), 0, dp(12), dp(4))
    }
    bar.addView(
        Oni.chip(this, "+ Nova animação", active = true, textSizeSp = 12f).also {
            it.setOnClickListener { newAnimDialog() }
        },
        LinearLayout.LayoutParams(0, dp(48), 1f)
    )
    bar.addView(
        Oni.chip(this, "▶ Preview", textSizeSp = 12f).also {
            it.setOnClickListener { toggleAnimPreview() }
        },
        LinearLayout.LayoutParams(0, dp(48), 1f)
    )
    animList = ListView(this).apply {
        divider = null
        dividerHeight = 0
        selector = android.graphics.drawable.ColorDrawable(0)
        setPadding(dp(8), dp(4), dp(8), dp(8))
        clipToPadding = false
        onItemClickListener =
            AdapterView.OnItemClickListener { _, _, position, _ ->
                animNames.getOrNull(position)?.let { animMenuDialog(it) }
            }
    }
    val empty = Oni.emptyState(
        this, "◍", "Nenhuma animação no projeto.",
        "+ Nova animação"
    ) { newAnimDialog() }
    val aList = animList!!
    aList.tag = empty
    val host = FrameLayout(this)
    host.addView(aList, FrameLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.MATCH_PARENT))
    host.addView(empty, FrameLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT,
        Gravity.CENTER).apply { setMargins(dp(24), dp(8), dp(24), 0) })
    panelContainer.addView(
        bar,
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        )
    )
    panelContainer.addView(
        host,
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT
        )
    )
}

fun EditorActivity.refreshAnim() {
    val act = this
    val list = animList ?: return
    if (handle == 0L) return
    val tsv = EditorJni.nativeEditorAnimationList(handle) ?: return
    animNames.clear()
    val display = mutableListOf<Pair<String, String>>()  // nome / resumo
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
            animNames.last() to
                "$clip  ${"%.1f".format(dur)}s  ${frames}f ${keys}k" +
                (if (loop) " ∞" else "")
        )
    }
    list.adapter = object : ArrayAdapter<Pair<String, String>>(
        this, R.layout.oni_list_item, display
    ) {
        override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
            val (name, summary) = display[position]
            val row = Oni.listRow(act, twoLine = true)
            val col = LinearLayout(act).apply {
                orientation = LinearLayout.VERTICAL
            }
            col.addView(TextView(act).apply {
                text = name
                setTextColor(Oni.TEXT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                typeface = Typeface.DEFAULT_BOLD
            })
            col.addView(TextView(act).apply {
                text = summary
                setTextColor(Oni.TEXT_DIM)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                typeface = Typeface.MONOSPACE
            })
            row.addView(col, LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            row.addView(TextView(act).apply {
                text = "›"
                setTextColor(Oni.TEXT_DIM)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            })
            return row
        }
    }
    (list.tag as? View)?.visibility =
        if (animNames.isEmpty()) View.VISIBLE else View.GONE
}

fun EditorActivity.newAnimDialog() {
    val act = this
    inputDialog("Nome da animação", "walk") { name ->
        if (EditorJni.nativeEditorAnimationCreate(handle, name)) {
            toastOk("Animação criada — adicione frames com texturas reais")
            refreshAnim()
        } else {
            toastErr(lastErrorText())
        }
    }
}

/** Preview liga/desliga na SELEÇÃO (o clip precisa estar anexado ou é
 *  escolhido pelo nome do painel). */
fun EditorActivity.toggleAnimPreview() {
    val act = this
    if (EditorJni.nativeEditorPreviewing(handle)) {
        EditorJni.nativeEditorPreviewStop(handle)
        toast("Preview parado — transform restaurado")
        return
    }
    if (selection == 0L) {
        toastErr("Selecione uma entidade e anexe uma animação primeiro")
        return
    }
    // Pega o clip do Animator da seleção (Inspector path canônico).
    val fields = EditorJni.nativeEditorComponentFields(
        handle, selection, "eng::animation::Animator"
    ) ?: run {
        toastErr("Entidade sem Animator — anexe no painel Animação"); return
    }
    val clip = fields.lines().firstOrNull { it.startsWith("clip\t") }
        ?.split('\t')?.getOrNull(2)
    if (clip.isNullOrBlank()) {
        toastErr("Animator sem clip")
        return
    }
    if (EditorJni.nativeEditorPreviewStart(handle, selection, clip)) {
        toastOk("Preview RODANDO (clip '$clip')")
    } else {
        toastErr(lastErrorText())
    }
}

// --- linhas de campo do Inspector (P0-6, ADR-052) -----------------------------------

// --- P4.6 (Bloco 1): bitfields nomeados (chips no Inspector) ------------------

/// Estado vivo de uma row de bitfield (chips + valor corrente) — tag da
/// row para o sync diferencial (mesmo espírito do B-B: in-place, sem
/// recriar a view).
internal class BitfieldHolder {
    var value: Long = 0L
    val chips = mutableListOf<Pair<TextView, Long>>()
    var extraLabel: TextView? = null
}

/** Campos cuja edição correta é por NOME de camada (bitfield nomeado). */
internal fun bitfieldKindOf(component: String, path: String): Boolean {
    // P4.6 (Bloco 1): Collider.layer/mask. O Bloco 2 (luz) estende aqui
    // (Light2D.mask / SpriteData.lightLayer).
    return component == "eng::physics::Collider" &&
        (path == "layer" || path == "mask")
}

/** Re-estila os chips do holder pelo valor corrente (ativo = ACCENT). */
internal fun EditorActivity.styleBitfieldChips(holder: BitfieldHolder) {
    for ((chip, bit) in holder.chips) {
        val active = (holder.value and bit) != 0L
        if (active) {
            chip.setTextColor(Oni.ON_ACCENT)
            chip.background = Oni.ripplePill(this, Oni.ACCENT)
        } else {
            chip.setTextColor(Oni.TEXT)
            chip.background = Oni.ripplePill(this, Oni.RAISED)
        }
    }
    val extra = holder.value and holder.chips.fold(0L) { acc, (_, bit) ->
        acc and bit.inv()
    }
    holder.extraLabel?.let { label ->
        label.text = "0x%08X".format(extra)
        label.visibility = if (extra != 0L) View.VISIBLE else View.GONE
    }
}

/**
 * P4.6 (Bloco 1): row de bitfield nomeado — chips toggle (um por camada
 * nomeada no project settings) + mono com bits SEM nome (honesto: bits
 * fora da tabela aparecem, não somem). Escrita via setFieldQuiet (inteiro);
 * visual re-estilizado in-place (zero rebuild — o foco de outros campos
 * sobrevive, regra N2).
 */
internal fun EditorActivity.addBitfieldRow(
    parent: LinearLayout, component: String, path: String, value: String
) {
    val holder = BitfieldHolder()
    holder.value = value.toLongOrNull() ?: 0L

    parent.addView(labelView(prettyFieldLabel(path)))
    val row = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
    }
    val chipRow = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
    }
    val table = EditorJni.nativeEditorCollisionLayerList(handle) ?: ""
    for (line in table.lines().filter { it.isNotEmpty() }) {
        val parts = line.split('\t')
        if (parts.size < 2) continue
        val bit = parts[1].toLongOrNull() ?: continue
        if (bit == 0L) continue
        val name = parts[0]
        val chip = Oni.chip(this, name, active = (holder.value and bit) != 0L,
                            textSizeSp = 12f).apply {
            setOnClickListener {
                // Valor fresco do holder (sync diferencial mantém veraz).
                val newValue = (holder.value xor bit) and 0xFFFFFFFFL
                setFieldQuiet(component, path, newValue.toString())
                holder.value = newValue
                styleBitfieldChips(holder)
            }
        }
        holder.chips.add(chip to bit)
        chipRow.addView(
            chip,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, dp(48))
        )
    }
    // Bits setados fora da tabela nomeada — visíveis, nunca silenciados.
    val extra = TextView(this).apply {
        setTextColor(Oni.TEXT_DIM)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
        typeface = Typeface.MONOSPACE
        setPadding(dp(10), 0, dp(10), 0)
        visibility = View.GONE
    }
    holder.extraLabel = extra
    row.addView(chipRow, LinearLayout.LayoutParams(
        0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
    row.addView(extra, LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.WRAP_CONTENT, dp(48)))
    parent.addView(row)
    styleBitfieldChips(holder)
    // Sync diferencial (P4.2/B-B): a row inteira re-estila in-place quando
    // o valor muda por fora (undo/gizmo/outra via).
    inspectorBitfieldRows["$component\u0001$path"] = row
    row.tag = holder
}

/**
 * Linha de campo do Inspector: o kind semântico vindo do C++ decide o
 * editor — Switch (bool), dropdown (enum), swatch+sliders (color), picker
 * de textura, campo numérico ou texto. Nada de digitar "true"/"Sphere"/hex
 * à mão. Superfície 100% Oni (§2).
 */
fun EditorActivity.addFieldRow(
    parent: LinearLayout, component: String, path: String,
    typeName: String, value: String, kind: String, options: String
) {
    val act = this
    // P4.6 (Bloco 1): bitfields nomeados vêm como kind "int" — a edição
    // correta é por NOME de camada (chips), não por número cru.
    if (kind == "int" && bitfieldKindOf(component, path)) {
        addBitfieldRow(parent, component, path, value)
        return
    }
    when (kind) {
        "bool" -> {
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            row.addView(
                labelView(prettyFieldLabel(path)),
                LinearLayout.LayoutParams(0, dp(48), 1f)
            )
            val sw = Oni.switch(this).apply {
                isChecked = value == "true"
                setOnCheckedChangeListener { _, checked ->
                    setFieldQuiet(component, path, if (checked) "true" else "false")
                }
            }
            row.addView(sw, LinearLayout.LayoutParams(dp(56), dp(48)))
            parent.addView(row)
            return
        }
        "enum" -> {
            val choices = options.split('|').filter { it.isNotEmpty() }
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            row.addView(
                labelView(prettyFieldLabel(path)),
                LinearLayout.LayoutParams(0, dp(48), 0.9f)
            )
            val current = Oni.chip(this, value, mono = true, textSizeSp = 13f).apply {
                setOnClickListener {
                    OniDialog.choice(
                        act, prettyFieldLabel(path),
                        choices, choices.indexOf(value)
                    ) { which ->
                        setFieldQuiet(component, path, choices[which])
                        refreshInspector()
                    }
                }
            }
            // P4.2 (B-B): registrado p/ sync diferencial (valor in-place).
            inspectorValueViews["$component\u0001$path"] = Pair(current, "")
            row.addView(current, LinearLayout.LayoutParams(0, dp(48), 1.1f))
            parent.addView(row)
            return
        }
        "color" -> {
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            row.addView(
                labelView(prettyFieldLabel(path)),
                LinearLayout.LayoutParams(0, dp(48), 0.9f)
            )
            val initial = parseHexColor(value) ?: 0xFFFFFFFF.toInt()
            val swatch = TextView(this).apply {
                text = value
                setTextColor(Oni.TEXT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
                typeface = Typeface.MONOSPACE
                gravity = Gravity.CENTER_VERTICAL or Gravity.END
                setPadding(dp(12), dp(6), dp(12), dp(6))
                background = Oni.ripple(
                    act,
                    initial and 0xFFFFFF or 0xFF000000.toInt(), Oni.R_THUMB)
                setOnClickListener {
                    colorPickerDialog(component, path, value, initial) { hex, argb ->
                        background = Oni.ripple(
                            act, argb, Oni.R_THUMB)
                        text = hex
                    }
                }
            }
            row.addView(swatch, LinearLayout.LayoutParams(0, dp(48), 1.1f))
            parent.addView(row)
            return
        }
        "texture" -> {
            parent.addView(labelView(prettyFieldLabel(path)))
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            val current = TextView(this).apply {
                text = value.ifEmpty { "(nenhuma)" }
                setTextColor(Oni.ACCENT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
                typeface = Typeface.MONOSPACE
            }
            // P4.2 (B-B): registrado p/ sync diferencial.
            inspectorValueViews["$component\u0001$path"] = Pair(current, "(nenhuma)")
            row.addView(current, LinearLayout.LayoutParams(0, dp(48), 1f))
            row.addView(
                Oni.chip(this, "Escolher…", textSizeSp = 12f).apply {
                    setOnClickListener {
                        pickTextureFor(component, path) { chosen ->
                            current.text = chosen.ifEmpty { "(nenhuma)" }
                        }
                    }
                },
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, dp(48))
            )
            parent.addView(row)
            return
        }
        "audio" -> {
            // P2 (§12): AudioSource.soundAsset — picker de WAVs do projeto
            // + preview que toca AGORA (mesma via do Play: mixer real).
            parent.addView(labelView(prettyFieldLabel(path)))
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            val current = TextView(this).apply {
                text = value.ifEmpty { "(nenhum)" }
                setTextColor(Oni.ACCENT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
                typeface = Typeface.MONOSPACE
            }
            // P4.2 (B-B): registrado p/ sync diferencial.
            inspectorValueViews["$component\u0001$path"] = Pair(current, "(nenhum)")
            row.addView(current, LinearLayout.LayoutParams(0, dp(48), 1f))
            row.addView(
                Oni.chip(this, "▶", textSizeSp = 12f).apply {
                    // P4.3 (N1): TOGGLE — 2º toque para (label muda com o
                    // estado real do mixer nativo).
                    text = if (EditorJni.nativeEditorAudioPreviewPlaying(
                                handle)
                            ) "■" else "▶"
                    setOnClickListener {
                        if (value.isNotBlank()) {
                            // N1: não estava a tocar e não passou a tocar
                            // = start falhou → erro explícito (o stop
                            // intencional não é erro).
                            val wasPlaying =
                                EditorJni.nativeEditorAudioPreviewPlaying(handle)
                            val nowPlaying = toggleAudioPreview(value)
                            text = if (nowPlaying) "■" else "▶"
                            if (!wasPlaying && !nowPlaying) {
                                toastErr(lastErrorText())
                            }
                        }
                    }
                },
                LinearLayout.LayoutParams(dp(48), dp(48))
            )
            row.addView(
                Oni.chip(this, "Escolher…", textSizeSp = 12f).apply {
                    setOnClickListener {
                        pickAudioFor(component, path) { chosen ->
                            current.text = chosen.ifEmpty { "(nenhum)" }
                        }
                    }
                },
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, dp(48))
            )
            parent.addView(row)
            return
        }
        "material" -> {
            // P3 §3: SpriteData.materialAsset — picker de materiais do
            // projeto (vazio = default lit neutro).
            parent.addView(labelView(prettyFieldLabel(path)))
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            val current = TextView(this).apply {
                text = value.ifEmpty { "(default lit)" }
                setTextColor(Oni.ACCENT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
                typeface = Typeface.MONOSPACE
            }
            // P4.2 (B-B): registrado p/ sync diferencial.
            inspectorValueViews["$component\u0001$path"] = Pair(current, "(default lit)")
            row.addView(current, LinearLayout.LayoutParams(0, dp(48), 1f))
            row.addView(
                Oni.chip(this, "Escolher…", textSizeSp = 12f).apply {
                    setOnClickListener {
                        pickMaterialFor(component, path) { chosen ->
                            current.text = chosen.ifEmpty { "(default lit)" }
                        }
                    }
                },
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, dp(48))
            )
            parent.addView(row)
            return
        }
    }

    // number/int/text → campo curvo (numérico com mono quando aplicável).
    val numeric = kind == "number" || kind == "int"
    val row = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
    }
    row.addView(
        labelView(prettyFieldLabel(path)),
        LinearLayout.LayoutParams(0, dp(48), 0.7f)
    )
    val edit = Oni.field(this, mono = numeric).apply {
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
        imeOptions = android.view.inputmethod.EditorInfo.IME_ACTION_DONE
        setOnEditorActionListener { _, actionId, _ ->
            if (actionId == android.view.inputmethod.EditorInfo.IME_ACTION_DONE) {
                setFieldQuiet(component, path, text.toString())
                true
            } else false
        }
    }
    // P4.2 (B-B): EditText registrado p/ sync diferencial — o mesmo
    // campo é ATUALIZADO (sem foco) em vez de recriado a cada refresh.
    inspectorTextFields["$component\u0001$path"] = edit
    row.addView(
        edit,
        LinearLayout.LayoutParams(0, dp(48), 1.3f)
    )
    parent.addView(row)
}
