package com.goni.runtime

import android.app.Dialog
import android.graphics.Typeface
import android.graphics.drawable.ColorDrawable
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.TextWatcher
import android.text.style.ForegroundColorSpan
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/**
 * P4.5 (§2) — JANELA DEDICADA do editor de scripts NI-Script (ADR-053).
 *
 * Features do prompt: card curvo; superfície de código #0D1117; mono 13sp;
 * syntax coloring (keywords acento, strings success, comentários
 * secundário, números warn); numeração de linhas; toolbar flutuante
 * Compilar/Anexar/Salvar; painéis laterais colapsáveis (variables/
 * functions); auto-indent. Comportamento P0-7 preservado (compilar com
 * diagnósticos line:col, anexar à seleção, salvar — o MESMO JNI).
 *
 * Performance (Unisoc): colorização com debounce 250 ms e spans de cor
 * APENAS (texto nunca mutado pelo highlight — cursor/IME seguros);
 * numeração recalculada no change (contagem de '\n' — barato).
 */
class ScriptWindow(private val activity: EditorActivity) {

    private lateinit var code: EditText
    private lateinit var lineNumbers: TextView
    private lateinit var diagLine: TextView
    private lateinit var varsPanel: LinearLayout
    private lateinit var funcsPanel: LinearLayout
    private var scriptName: String = ""
    private var dialog: Dialog? = null
    private val handler = Handler(Looper.getMainLooper())
    private var highlightPending = false
    private var autoIndenting = false

    // NI-Script (engine/niscript/src/Lexer.cpp kKeywords — mesma tabela).
    private val keywords = setOf(
        "f", "stop", "up", "if", "else", "repeat", "repair", "timeout",
        "link", "to", "emit", "give", "var", "add", "and", "or", "not",
        "true", "false"
    )

    fun open(name: String) {
        val content = EditorJni.nativeEditorScriptRead(activity.handle, name)
        if (content == null) {
            activity.toastErr(activity.lastErrorText())
            return
        }
        scriptName = name
        build(content)
    }

    private fun build(content: String) {
        val act = activity
        val ctx = act

        val card = FrameLayout(ctx).apply {
            background = Oni.rounded(ctx, Oni.PANEL, Oni.R_DIALOG)
            setPadding(Oni.dp(ctx, 10), Oni.dp(ctx, 10),
                Oni.dp(ctx, 10), Oni.dp(ctx, 10))
        }

        // --- coluna principal: header + código + diag ---------------------
        val column = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
        }

        val header = LinearLayout(ctx).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(Oni.dp(ctx, 6), 0, 0, Oni.dp(ctx, 8))
        }
        header.addView(TextView(ctx).apply {
            text = scriptName
            setTextColor(Oni.TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            typeface = Typeface.MONOSPACE
        }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        // Painéis laterais colapsáveis (variables/functions — §2).
        val btnVars = Oni.chip(ctx, "x", mono = true, textSizeSp = 12f).apply {
            minimumWidth = Oni.dp(ctx, 44)
            setOnClickListener { togglePanel(varsPanel, this, "x") }
        }
        val btnFuncs = Oni.chip(ctx, "ƒ", textSizeSp = 13f).apply {
            minimumWidth = Oni.dp(ctx, 44)
            setOnClickListener { togglePanel(funcsPanel, this, "ƒ") }
        }
        header.addView(btnVars, LinearLayout.LayoutParams(Oni.dp(ctx, 48), Oni.dp(ctx, 48)))
        header.addView(btnFuncs, LinearLayout.LayoutParams(Oni.dp(ctx, 48), Oni.dp(ctx, 48)))
        header.addView(Oni.chip(ctx, "✕", textSizeSp = 13f).apply {
            setOnClickListener { dialog?.dismiss() }
        }, LinearLayout.LayoutParams(Oni.dp(ctx, 48), Oni.dp(ctx, 48)))
        column.addView(header)

        // --- superfície de código: números + código num ScrollView --------
        val codeSurface = LinearLayout(ctx).apply {
            orientation = LinearLayout.HORIZONTAL
            background = Oni.rounded(ctx, Oni.CODE_BG, Oni.R_FIELD)
            setPadding(Oni.dp(ctx, 8), Oni.dp(ctx, 8), Oni.dp(ctx, 8), Oni.dp(ctx, 8))
        }
        lineNumbers = TextView(ctx).apply {
            setTextColor(Oni.TEXT_DIM)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            typeface = Typeface.MONOSPACE
            gravity = Gravity.TOP or Gravity.END
            setPadding(0, Oni.dp(ctx, 10), Oni.dp(ctx, 8), Oni.dp(ctx, 10))
            includeFontPadding = false
        }
        codeSurface.addView(lineNumbers, LinearLayout.LayoutParams(
            Oni.dp(ctx, 30), ViewGroup.LayoutParams.MATCH_PARENT))

        code = EditText(ctx).apply {
            setText(content)
            setTextColor(Oni.TEXT)
            setHintTextColor(Oni.TEXT_DIM)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            typeface = Typeface.MONOSPACE
            setBackgroundColor(0)
            setPadding(Oni.dp(ctx, 4), Oni.dp(ctx, 8), Oni.dp(ctx, 4), Oni.dp(ctx, 10))
            setHorizontallyScrolling(true)
            gravity = Gravity.TOP
            inputType = android.text.InputType.TYPE_CLASS_TEXT or
                android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE or
                android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            imeOptions = android.view.inputmethod.EditorInfo.IME_FLAG_NO_ENTER_ACTION
            addTextChangedListener(object : TextWatcher {
                private var beforeCount = 0

                override fun beforeTextChanged(
                    s: CharSequence?, start: Int, count: Int, after: Int
                ) { beforeCount = s?.length ?: 0 }

                override fun onTextChanged(
                    s: CharSequence?, start: Int, count: Int, after: Int
                ) {}

                override fun afterTextChanged(s: Editable?) {
                    if (autoIndenting || s == null) return
                    updateLineNumbers(s)
                    scheduleHighlight()
                    autoIndent(s, beforeCount)
                }
            })
        }
        codeSurface.addView(code, LinearLayout.LayoutParams(
            0, ViewGroup.LayoutParams.MATCH_PARENT, 1f))
        column.addView(codeSurface, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f).apply {
            topMargin = Oni.dp(ctx, 2)
        })

        // --- linha de diagnóstico (compilar preenche) -----------------------
        diagLine = TextView(ctx).apply {
            text = "—"
            setTextColor(Oni.TEXT_DIM)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            typeface = Typeface.MONOSPACE
            setPadding(Oni.dp(ctx, 6), Oni.dp(ctx, 6), Oni.dp(ctx, 6), 0)
        }
        column.addView(diagLine)

        // --- painéis colapsáveis (vars/funcs) --------------------------------
        varsPanel = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            visibility = View.GONE
            background = Oni.rounded(ctx, Oni.RAISED, Oni.R_FIELD)
            setPadding(Oni.dp(ctx, 8), Oni.dp(ctx, 4), Oni.dp(ctx, 8), Oni.dp(ctx, 4))
        }
        funcsPanel = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            visibility = View.GONE
            background = Oni.rounded(ctx, Oni.RAISED, Oni.R_FIELD)
            setPadding(Oni.dp(ctx, 8), Oni.dp(ctx, 4), Oni.dp(ctx, 8), Oni.dp(ctx, 4))
        }
        column.addView(varsPanel, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT))
        column.addView(funcsPanel, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT))

        card.addView(column, FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT))

        // --- toolbar flutuante (Compilar/Anexar/Salvar) -----------------------
        val toolbar = LinearLayout(ctx).apply {
            orientation = LinearLayout.HORIZONTAL
            background = Oni.pill(ctx, 0xF21A2029.toInt())
            setPadding(Oni.dp(ctx, 6), Oni.dp(ctx, 6), Oni.dp(ctx, 6), Oni.dp(ctx, 6))
        }
        toolbar.addView(Oni.chip(ctx, "Compilar", active = true, textSizeSp = 13f).apply {
            setOnClickListener { compile() }
        }, LinearLayout.LayoutParams(0, Oni.dp(ctx, 48), 1f))
        toolbar.addView(Oni.chip(ctx, "Anexar", textSizeSp = 13f).apply {
            setOnClickListener { attach() }
        }, LinearLayout.LayoutParams(0, Oni.dp(ctx, 48), 1f))
        toolbar.addView(Oni.chip(ctx, "Salvar", active = true, textSizeSp = 13f).apply {
            setOnClickListener { save() }
        }, LinearLayout.LayoutParams(0, Oni.dp(ctx, 48), 1f))
        card.addView(toolbar, FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT,
            Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL
        ).apply { setMargins(Oni.dp(ctx, 8), 0, Oni.dp(ctx, 8), Oni.dp(ctx, 10)) })

        val root = FrameLayout(ctx).apply {
            setBackgroundColor(0x66000000)
            setPadding(Oni.dp(ctx, 8), Oni.dp(ctx, 8), Oni.dp(ctx, 8), Oni.dp(ctx, 8))
        }
        root.addView(card, FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT))

        val dlg = Dialog(act)
        dlg.requestWindowFeature(android.view.Window.FEATURE_NO_TITLE)
        dlg.setContentView(root)
        dlg.window?.let { w ->
            w.setBackgroundDrawable(ColorDrawable(0))
            w.setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT)
            w.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
        }
        dialog = dlg
        dlg.show()
        updateLineNumbers(code.text)
        highlight(code.editableText ?: Editable.Factory.getInstance().newEditable(""))
    }

    // --- ações (mesmo JNI do P0-7 — zero mudança de comportamento) ----------

    private fun compile() {
        val tsv = EditorJni.nativeEditorScriptCompile(
            activity.handle, code.text.toString())
        if (tsv == null) {
            activity.toastErr(activity.lastErrorText())
            return
        }
        val lines = tsv.lines()
        val ok = lines.firstOrNull() == "1"
        val rows = lines.drop(1).filter { it.isNotBlank() }
        diagLine.text = if (ok) "✓ compila até bytecode"
        else "✗ ${rows.size} erro(s) — 1º: ${rows.firstOrNull()?.split('\t')
            ?.take(2)?.joinToString(":") { it } ?: ""}"
        diagLine.setTextColor(if (ok) Oni.SUCCESS else Oni.DANGER)
        activity.showCompileDiags(tsv)
    }

    private fun attach() {
        if (activity.selection == 0L) {
            activity.toastErr("Selecione uma entidade antes de anexar")
            return
        }
        if (EditorJni.nativeEditorScriptAssign(
                activity.handle, activity.selection, scriptName)) {
            activity.toastOk("Anexado a ${activity.currentEntityName(activity.selection)}")
        } else {
            activity.toastErr(activity.lastErrorText())
        }
    }

    private fun save() {
        if (EditorJni.nativeEditorScriptWrite(
                activity.handle, scriptName, code.text.toString())) {
            activity.toastOk("Salvo")
            if (activity.activePanel == EditorActivity.PANEL_SCRIPTS) {
                activity.refreshScripts()
            }
        } else {
            activity.toastErr(activity.lastErrorText())
        }
    }

    // --- painéis colapsáveis ---------------------------------------------------

    private fun togglePanel(panel: LinearLayout, chip: TextView, glyph: String) {
        val show = panel.visibility == View.GONE
        panel.visibility = if (show) View.VISIBLE else View.GONE
        chip.setTextColor(if (show) Oni.ON_ACCENT else Oni.TEXT)
        chip.background =
            if (show) Oni.ripplePill(activity, Oni.ACCENT)
            else Oni.ripplePill(activity, Oni.RAISED)
        if (show) fillPanel(panel)
    }

    private fun fillPanel(panel: LinearLayout) {
        panel.removeAllViews()
        val src = code.text.toString()
        val isVars = panel === varsPanel
        val regex = if (isVars) Regex("\\bvar\\s+([A-Za-z_]\\w*)")
                    else Regex("\\b(?:up|f)\\s+([A-Za-z_]\\w*)")
        val names = LinkedHashSet<String>()
        for (m in regex.findAll(src)) names.add(m.groupValues[1])
        if (names.isEmpty()) {
            panel.addView(TextView(activity).apply {
                text = if (isVars) "(sem variáveis)" else "(sem handlers)"
                setTextColor(Oni.TEXT_DIM)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                typeface = Typeface.MONOSPACE
            })
            return
        }
        for (name in names) {
            val row = Oni.listRow(activity)
            row.minimumHeight = Oni.dp(activity, 40)
            row.addView(TextView(activity).apply {
                text = (if (isVars) "var " else "") + name
                setTextColor(Oni.ACCENT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
                typeface = Typeface.MONOSPACE
            }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            row.setOnClickListener {
                // Insere o nome no cursor (painel = ferramenta de escrita).
                val pos = code.selectionStart.coerceIn(0, code.length())
                code.text.insert(pos, name)
            }
            panel.addView(row)
        }
    }

    // --- numeração de linhas ------------------------------------------------------

    private fun updateLineNumbers(s: CharSequence) {
        val count = s.count { it == '\n' } + 1
        val sb = StringBuilder()
        for (i in 1..count) {
            sb.append(i).append('\n')
        }
        lineNumbers.text = sb.toString()
    }

    // --- auto-indent (§2): nova linha herda o indent da anterior; linha
    // que termina em ':' indenta +4 (blocos do NI-Script).

    private fun autoIndent(s: Editable, beforeLen: Int) {
        val cursor = code.selectionStart
        if (cursor <= 0 || cursor > s.length) return
        if (s.length != beforeLen + 1) return
        if (s[cursor - 1] != '\n') return
        // indent da linha anterior
        val lineStart = s.lastIndexOf('\n', cursor - 2).let {
            if (it < 0) 0 else it + 1
        }
        var indent = 0
        while (lineStart + indent < cursor - 1 &&
            s[lineStart + indent] == ' ') indent++
        // linha anterior termina em ':' → bloco abre (+4 espaços)
        val prevLine = s.substring(lineStart, cursor - 1).trimEnd()
        val extra = if (prevLine.endsWith(":")) 4 else 0
        val insert = " ".repeat(indent + extra)
        if (insert.isEmpty()) return
        autoIndenting = true
        s.insert(cursor, insert)
        code.setSelection(cursor + insert.length)
        autoIndenting = false
        updateLineNumbers(s)
    }

    // --- syntax coloring (debounce 250 ms — barato no Unisoc) ----------------

    private fun scheduleHighlight() {
        if (highlightPending) return
        highlightPending = true
        handler.postDelayed({
            highlightPending = false
            code.editableText?.let { highlight(it) }
        }, 250)
    }

    private fun highlight(text: Editable) {
        // Remove APENAS os spans de cor próprios (o resto do texto fica).
        for (span in text.getSpans(
                0, text.length, ForegroundColorSpan::class.java)) {
            text.removeSpan(span)
        }
        val content = text.toString()
        // Strings primeiro (o que estiver dentro vence keywords).
        for (m in Regex("\"[^\"]*\"").findAll(content)) {
            text.setSpan(ForegroundColorSpan(Oni.CODE_STRING),
                m.range.first, m.range.last + 1,
                android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        // Comentários '#...' até o fim da linha.
        for (m in Regex("#[^\\n]*").findAll(content)) {
            text.setSpan(ForegroundColorSpan(Oni.TEXT_DIM),
                m.range.first, m.range.last + 1,
                android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        // Keywords (identificador inteiro — borda de palavra).
        for (m in Regex("\\b[A-Za-z_][A-Za-z0-9_]*\\b").findAll(content)) {
            if (m.value in keywords) {
                text.setSpan(ForegroundColorSpan(Oni.ACCENT),
                    m.range.first, m.range.last + 1,
                    android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
        }
        // Números (int/float).
        for (m in Regex("\\b\\d+(\\.\\d+)?\\b").findAll(content)) {
            text.setSpan(ForegroundColorSpan(Oni.CODE_NUMBER),
                m.range.first, m.range.last + 1,
                android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
    }
}
