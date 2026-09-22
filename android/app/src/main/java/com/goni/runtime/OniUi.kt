package com.goni.runtime

import android.app.Dialog
import android.content.Context
import android.graphics.Typeface
import android.graphics.drawable.ClipDrawable
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.LayerDrawable
import android.graphics.drawable.RippleDrawable
import android.graphics.drawable.StateListDrawable
import android.os.Handler
import android.os.Looper
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.Window
import android.view.animation.AnimationUtils
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.Space
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast

/**
 * P4.5 — Sistema de design "CURVED DARK" (prompt §1).
 *
 * Regras que tudo obedece:
 *  1. Sem bordas — separação por tom em camadas; borda 1dp SÓ no foco de input.
 *  2. Escala de curvatura — chips/pills raio total; botões 16dp; campos 14dp;
 *     cards 20dp; diálogos 24dp; topo de sheets 28dp; FABs circulares;
 *     thumbs 12dp.
 *  3. Flutuante, não docked — nada colado na aresta.
 *  4. Luz como hierarquia — acento #8AB4F8 só em interativo/ativo; semânticas
 *     (#4CC38A/#E5B567/#E57373) só em estado; laranja/ciano de seleção/gizmo
 *     preservados (§3 — intocados).
 *  5. Tipo — 12/14/16/20sp; mono para números/código; uppercase 12sp só em
 *     headers de secção.
 *  6. Motion barato (Unisoc) — sheets 180 ms slide; diálogos 120 ms fade+scale;
 *     pressed = overlay 12% + scale 95%.
 *  7. Alvos >= 48dp em tudo, sempre.
 */
object Oni {
    // --- camadas de tom (§1.1) ------------------------------------------------
    val BG = 0xFF0B0E13.toInt()         // fundo geral (canvas de carvão)
    val PANEL = 0xFF12161D.toInt()      // painéis/barras
    val RAISED = 0xFF1A2029.toInt()     // raised (campos, rows, botões)
    val OVERLAY = 0xFF222933.toInt()    // overlay (diálogos, toasts, pills)
    val SCRIM = 0xA00B0E13.toInt()      // scrim sobre o viewport
    val HANDLE = 0x3DE6EDF3.toInt()     // drag-handle do sheet
    val PRESSED = 0x1FE6EDF3.toInt()    // overlay 12% (§1.6)
    val FOCUS_STROKE = 0xFF8AB4F8.toInt()

    // --- acento + semânticas (§1.4) --------------------------------------------
    val ACCENT = 0xFF8AB4F8.toInt()     // marca G.ONI — só interativo/ativo
    val ON_ACCENT = 0xFF0B0E13.toInt()  // texto sobre acento
    val SUCCESS = 0xFF4CC38A.toInt()
    val WARN = 0xFFE5B567.toInt()
    val DANGER = 0xFFE57373.toInt()
    val SEL = 0xFFE8A33D.toInt()        // seleção — PRESERVADO (§3)
    val GIZMO = 0xFF5BC8E8.toInt()      // gizmo — PRESERVADO (§3)

    // --- texto -------------------------------------------------------------------
    val TEXT = 0xFFE6EDF3.toInt()
    val TEXT_DIM = 0xFF8B949E.toInt()

    // --- superfície de código (§2 janela de script) -------------------------------
    val CODE_BG = 0xFF0D1117.toInt()
    val CODE_STRING = SUCCESS          // strings — success (§2)
    val CODE_NUMBER = WARN             // números — warn (§2)

    // --- raio (§1.2) ---------------------------------------------------------------
    const val R_BTN = 16        // dp
    const val R_FIELD = 14      // dp
    const val R_CARD = 20       // dp
    const val R_DIALOG = 24     // dp
    const val R_SHEET = 28      // dp (topo)
    const val R_THUMB = 12      // dp
    const val R_PILL = 999      // raio total
    const val PILL = R_PILL

    // severidade de toast (§2)
    const val TOAST_INFO = 0
    const val TOAST_OK = 1
    const val TOAST_ERR = 2

    fun dp(c: Context, v: Int): Int =
        TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, v.toFloat(), c.resources.displayMetrics
        ).toInt()

    // --- drawables base ------------------------------------------------------------

    /** Fundo arredondado simples. */
    fun rounded(c: Context, color: Int, radiusDp: Int): GradientDrawable =
        GradientDrawable().apply {
            setColor(color)
            cornerRadius = dp(c, radiusDp).toFloat()
        }

    /** Fundo arredondado com raios por canto (8 floats — topo de sheet 28dp). */
    fun roundedCorners(c: Context, color: Int, radiiDp: FloatArray): GradientDrawable =
        GradientDrawable().apply {
            setColor(color)
            cornerRadii = FloatArray(8) { dp(c, 1) * radiiDp[it] }
        }

    /** Pill — raio total (chips, toasts, clusters). */
    fun pill(c: Context, color: Int): GradientDrawable = rounded(c, color, PILL)

    /** Fundo arredondado + ripple 12% (§1.6) — sem borda, sem lib. */
    fun ripple(c: Context, color: Int, radiusDp: Int): RippleDrawable =
        RippleDrawable(
            android.content.res.ColorStateList.valueOf(PRESSED),
            rounded(c, color, radiusDp), null
        )

    /** Ripple transparente (row sem fundo — pressed overlay apenas). */
    fun rippleOnly(c: Context): RippleDrawable =
        RippleDrawable(
            android.content.res.ColorStateList.valueOf(PRESSED),
            null, null
        )

    /** Pill com ripple (chips clicáveis). */
    fun ripplePill(c: Context, color: Int): RippleDrawable =
        RippleDrawable(
            android.content.res.ColorStateList.valueOf(PRESSED),
            pill(c, color), null
        )

    /**
     * Pressed = overlay 12% + scale 95% (§1.6). O scale mora num
     * StateListAnimator programático — barato (2 animadores curtos).
     */
    fun pressScale(v: View) {
        val downX = android.animation.ObjectAnimator.ofFloat(v, View.SCALE_X, 0.95f)
        val downY = android.animation.ObjectAnimator.ofFloat(v, View.SCALE_Y, 0.95f)
        val upX = android.animation.ObjectAnimator.ofFloat(v, View.SCALE_X, 1f)
        val upY = android.animation.ObjectAnimator.ofFloat(v, View.SCALE_Y, 1f)
        downX.duration = 60; downY.duration = 60
        upX.duration = 90; upY.duration = 90
        val sla = android.animation.StateListAnimator()
        sla.addState(intArrayOf(android.R.attr.state_pressed), downX)
        sla.addState(intArrayOf(android.R.attr.state_pressed), downY)
        sla.addState(intArrayOf(), upX)
        sla.addState(intArrayOf(), upY)
        v.stateListAnimator = sla
    }

    /**
     * Campo filled curvo 14dp (§2): raised, SEM borda; foco = borda acento
     * (a ÚNICA borda da UI — §1.1).
     */
    fun field(c: Context, mono: Boolean = false, minLines_: Int = 0): EditText {
        val f = EditText(c)
        f.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        f.setTextColor(TEXT)
        f.setHintTextColor(0x668B949E.toInt())
        f.typeface = if (mono) Typeface.MONOSPACE else Typeface.DEFAULT
        f.background = fieldBg(c, focused = false)
        f.setOnFocusChangeListener { _, hasFocus ->
            f.background = fieldBg(c, focused = hasFocus)
        }
        val vPad = dp(c, 12)
        f.setPadding(dp(c, 14), vPad, dp(c, 14), vPad)
        f.minimumHeight = dp(c, 48)
        if (minLines_ > 0) f.minLines = minLines_
        return f
    }

    private fun fieldBg(c: Context, focused: Boolean): GradientDrawable =
        GradientDrawable().apply {
            setColor(RAISED)
            cornerRadius = dp(c, R_FIELD).toFloat()
            if (focused) {
                setStroke(dp(c, 1), FOCUS_STROKE)
            }
        }

    /** Switch curvo (§2): thumb 20dp acento no checked, track pill. */
    fun switch(c: Context): Switch {
        val s = Switch(c)
        val thumbOn = GradientDrawable().apply {
            shape = GradientDrawable.OVAL; setColor(ACCENT)
        }
        val thumbOff = GradientDrawable().apply {
            shape = GradientDrawable.OVAL; setColor(0xFFB0B8C4.toInt())
        }
        val trackOn = GradientDrawable().apply {
            cornerRadius = dp(c, PILL).toFloat(); setColor(0x558AB4F8.toInt())
        }
        val trackOff = GradientDrawable().apply {
            cornerRadius = dp(c, PILL).toFloat(); setColor(RAISED)
            setStroke(dp(c, 1), 0xFF2A3240.toInt())
        }
        val thumbs = StateListDrawable().apply {
            addState(intArrayOf(android.R.attr.state_checked), thumbOn)
            addState(intArrayOf(), thumbOff)
        }
        val tracks = StateListDrawable().apply {
            addState(intArrayOf(android.R.attr.state_checked), trackOn)
            addState(intArrayOf(), trackOff)
        }
        s.thumbDrawable = thumbs
        s.trackDrawable = tracks
        s.switchMinWidth = dp(c, 48)
        return s
    }

    /** Slider curvo (§2): track pill raised + fill acento, thumb 20dp acento,
     *  hit >= 48dp. */
    fun slider(c: Context): SeekBar {
        val sb = SeekBar(c)
        val bgShape = GradientDrawable().apply {
            cornerRadius = dp(c, PILL).toFloat(); setColor(RAISED)
        }
        val fillShape = GradientDrawable().apply {
            cornerRadius = dp(c, PILL).toFloat(); setColor(ACCENT)
        }
        val clip = ClipDrawable(fillShape, Gravity.START, ClipDrawable.HORIZONTAL)
        val layers = LayerDrawable(arrayOf(bgShape, clip)).apply {
            setId(0, android.R.id.background)
            setId(1, android.R.id.progress)
            setLayerHeight(0, dp(c, 6))
            setLayerGravity(0, Gravity.CENTER_VERTICAL)
            setLayerHeight(1, dp(c, 6))
            setLayerGravity(1, Gravity.CENTER_VERTICAL)
        }
        sb.progressDrawable = layers
        val thumb = GradientDrawable().apply {
            shape = GradientDrawable.OVAL; setColor(ACCENT)
        }
        thumb.setSize(dp(c, 20), dp(c, 20))
        sb.thumb = thumb
        sb.thumbOffset = dp(c, 10)
        sb.setPadding(dp(c, 14), 0, dp(c, 14), 0)
        sb.minimumHeight = dp(c, 48)
        sb.maxHeight = dp(c, 48)
        sb.splitTrack = false
        return sb
    }

    /** Botão base (§1): 48dp, curvo 16dp, ripple 12% + scale 95%. */
    fun button(
        c: Context, label: String, kind: Int = BTN_GHOST,
        mono: Boolean = false, textSizeSp: Float = 14f
    ): android.widget.Button {
        val b = android.widget.Button(c)
        b.text = label
        b.isAllCaps = false
        b.minHeight = 0
        b.minimumWidth = 0
        b.minWidth = 0
        b.stateListAnimator = null
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, textSizeSp)
        b.typeface = if (mono) Typeface.MONOSPACE else Typeface.DEFAULT
        val hPad = dp(c, 16)
        b.setPadding(hPad, 0, hPad, 0)
        b.minHeight = dp(c, 48)
        when (kind) {
            BTN_PRIMARY -> {
                b.setTextColor(ON_ACCENT)
                b.background = ripple(c, ACCENT, R_BTN)
            }
            BTN_DANGER -> {
                b.setTextColor(DANGER)
                b.background = ripple(c, RAISED, R_BTN)
            }
            BTN_SUCCESS -> {
                b.setTextColor(SUCCESS)
                b.background = ripple(c, RAISED, R_BTN)
            }
            BTN_SHEET -> {
                // ação sobre superfície overlay (diálogo/sheet)
                b.setTextColor(TEXT)
                b.background = ripple(c, RAISED, R_BTN)
            }
            else -> {
                b.setTextColor(TEXT)
                b.background = ripple(c, RAISED, R_BTN)
            }
        }
        pressScale(b)
        return b
    }

    const val BTN_GHOST = 0
    const val BTN_PRIMARY = 1
    const val BTN_DANGER = 2
    const val BTN_SUCCESS = 3
    const val BTN_SHEET = 4

    /** Chip pill (§1.2 — raio total); mono opcional; 48dp de alvo.
     *  P4.7.0 B3 (zero sobreposição): UMA linha SEMPRE — o wrap do
     *  weight antigo quebrava "Cena"→"Cen a" e "auto"→"a u t". */
    fun chip(c: Context, label: String, active: Boolean = false,
             mono: Boolean = false, textSizeSp: Float = 12f): TextView {
        val t = TextView(c)
        t.text = label
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, textSizeSp)
        t.typeface = if (mono) Typeface.MONOSPACE else Typeface.DEFAULT
        t.setPadding(dp(c, 14), 0, dp(c, 14), 0)
        t.gravity = Gravity.CENTER
        t.isSingleLine = true
        t.maxLines = 1
        t.minimumHeight = dp(c, 48)
        if (active) {
            t.setTextColor(ON_ACCENT)
            t.background = ripplePill(c, ACCENT)
        } else {
            t.setTextColor(TEXT)
            t.background = ripplePill(c, RAISED)
        }
        pressScale(t)
        return t
    }

    /** Text-button de diálogo (confirm/cancel nos cantos inferiores — §2). */
    fun dialogButton(c: Context, label: String, accent: Boolean): TextView {
        val t = TextView(c)
        t.text = label
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        t.setTextColor(if (accent) ACCENT else TEXT_DIM)
        t.isAllCaps = false
        t.gravity = Gravity.CENTER
        t.setPadding(dp(c, 16), 0, dp(c, 16), 0)
        t.minimumHeight = dp(c, 48)
        t.minimumWidth = dp(c, 72)
        t.background = rippleOnly(c)
        pressScale(t)
        return t
    }

    /** Row de lista (§2): sem borda/divisor, pressed overlay, 48dp. */
    fun listRow(c: Context, twoLine: Boolean = false): LinearLayout {
        val row = LinearLayout(c)
        row.orientation = LinearLayout.HORIZONTAL
        row.gravity = Gravity.CENTER_VERTICAL
        val vPad = if (twoLine) dp(c, 8) else 0
        row.setPadding(dp(c, 12), vPad, dp(c, 12), vPad)
        row.minimumHeight = dp(c, 48)
        row.background = rippleOnly(c)
        return row
    }

    /** Texto de linha (title/subtitle). */
    fun rowText(c: Context, text: String, dim: Boolean = false,
                mono: Boolean = false, sizeSp: Float = 14f): TextView {
        val t = TextView(c)
        t.text = text
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp)
        t.setTextColor(if (dim) TEXT_DIM else TEXT)
        t.typeface = if (mono) Typeface.MONOSPACE else Typeface.DEFAULT
        return t
    }

    /** Header de secção (uppercase 12sp — §1.5). */
    fun sectionHeader(c: Context, text: String): TextView =
        TextView(c).apply {
            this.text = text.uppercase()
            setTextColor(TEXT_DIM)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            typeface = Typeface.DEFAULT_BOLD
            setPadding(dp(c, 4), dp(c, 14), dp(c, 4), dp(c, 6))
            letterSpacing = 0.08f
        }

    /** Card de empty state (§2): ícone 48dp + dica + ação. */
    fun emptyState(
        c: Context, glyph: String, tip: String,
        actionLabel: String? = null, action: (() -> Unit)? = null
    ): LinearLayout {
        val card = LinearLayout(c).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(dp(c, 20), dp(c, 20), dp(c, 20), dp(c, 20))
            background = rounded(c, PANEL, R_CARD)
        }
        val icon = TextView(c).apply {
            this.text = glyph
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 20f)
            setTextColor(ACCENT)
            gravity = Gravity.CENTER
            background = rounded(c, RAISED, PILL)
        }
        card.addView(icon, LinearLayout.LayoutParams(dp(c, 48), dp(c, 48)).apply {
            gravity = Gravity.CENTER_HORIZONTAL
        })
        val tipView = TextView(c).apply {
            this.text = tip
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            setTextColor(TEXT_DIM)
            gravity = Gravity.CENTER
            setPadding(0, dp(c, 10), 0, 0)
        }
        card.addView(tipView, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT
        ))
        if (actionLabel != null && action != null) {
            card.addView(Space(c),
                LinearLayout.LayoutParams(1, dp(c, 12)))
            val btn = chip(c, actionLabel, active = true, textSizeSp = 13f)
            btn.setOnClickListener { action() }
            card.addView(btn, LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, dp(c, 48)
            ))
        }
        return card
    }

    /** Space helper. */
    fun space(c: Context, w: Int, h: Int): View = View(c).also {
        it.minimumWidth = dp(c, w); it.minimumHeight = dp(c, h)
    }
}

/**
 * P4.5 (§2) — Toast pill raised + ícone de severidade.
 * Substitui o Toast default; custom view via Toast.setView (o app está
 * sempre em FOREGROUND quando tosta — a restrição de background custom
 * toast nunca se aplica).
 */
object OniToast {
    private val main = Handler(Looper.getMainLooper())

    fun show(c: Context, text: String, severity: Int = Oni.TOAST_INFO) {
        val ctx = c.applicationContext
        val row = LinearLayout(ctx).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            background = Oni.pill(ctx, Oni.OVERLAY)
            setPadding(Oni.dp(ctx, 16), Oni.dp(ctx, 10), Oni.dp(ctx, 16), Oni.dp(ctx, 10))
        }
        val (glyph, color) = when (severity) {
            Oni.TOAST_OK -> "●" to Oni.SUCCESS
            Oni.TOAST_ERR -> "●" to Oni.DANGER
            else -> "●" to Oni.ACCENT
        }
        row.addView(TextView(ctx).apply {
            this.text = glyph
            setTextColor(color)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
            setPadding(0, 0, Oni.dp(ctx, 8), 0)
        })
        row.addView(TextView(ctx).apply {
            this.text = text
            setTextColor(Oni.TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        })
        val t = Toast(ctx)
        t.view = row
        t.duration = Toast.LENGTH_SHORT
        t.setGravity(Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL, 0,
            Oni.dp(ctx, 96))
        t.show()
    }
}

/**
 * P4.5 (§2) — Diálogo curvo 24dp (substitui ~25 usos de AlertDialog).
 * Card OVERLAY, título 16sp, conteúdo injetável, confirm/cancel nos cantos
 * inferiores, fade+scale 120 ms (res/anim). Sem AlertDialog em lugar nenhum.
 */
class OniDialog private constructor(private val context: Context) {
    private var titleText: String? = null
    private var contentView: View? = null
    private val buttons = mutableListOf<Btn>()

    class Btn(val label: String, val accent: Boolean = true,
              val danger: Boolean = false,
              val onClick: (() -> Unit)? = null)

    fun title(t: String): OniDialog { titleText = t; return this }
    fun content(v: View): OniDialog { contentView = v; return this }
    fun button(b: Btn): OniDialog { buttons.add(b); return this }

    fun show(): Dialog {
        val dialog = Dialog(context)
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE)

        val card = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            background = Oni.rounded(context, Oni.OVERLAY, Oni.R_DIALOG)
            setPadding(Oni.dp(context, 20), Oni.dp(context, 16),
                Oni.dp(context, 20), Oni.dp(context, 8))
        }
        titleText?.let {
            card.addView(TextView(context).apply {
                this.text = it
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
                setTextColor(Oni.TEXT)
                typeface = Typeface.DEFAULT_BOLD
            })
        }
        contentView?.let {
            val wrap = LinearLayout(context).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(0, Oni.dp(context, 12), 0, 0)
            }
            wrap.addView(it, LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT))
            card.addView(wrap)
        }
        if (buttons.isNotEmpty()) {
            val row = LinearLayout(context).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.END or Gravity.CENTER_VERTICAL
                setPadding(0, Oni.dp(context, 8), 0, 0)
            }
            for (b in buttons) {
                val tv = Oni.dialogButton(context, b.label, b.accent)
                if (b.danger) tv.setTextColor(Oni.DANGER)
                tv.setOnClickListener {
                    dialog.dismiss()
                    b.onClick?.invoke()
                }
                row.addView(tv, LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, Oni.dp(context, 48)))
            }
            card.addView(row)
        }

        // root transparente dá a margem flutuante do card.
        val root = FramePadding(context)
        root.setPadding(Oni.dp(context, 20), Oni.dp(context, 24),
            Oni.dp(context, 20), Oni.dp(context, 16))
        root.addView(card, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT))
        dialog.setContentView(root)

        dialog.window?.let { w ->
            w.setBackgroundDrawable(ColorDrawable(0))
            w.setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT)
            try {
                w.setWindowAnimations(R.style.OniDialogAnim)
            } catch (_: Exception) {
                // estilo ausente: sem animação (nunca crasha por visual)
            }
        }
        dialog.show()
        return dialog
    }

    /** FrameLayout com padding (evita import extra no topo). */
    private class FramePadding(c: Context) : android.widget.FrameLayout(c)

    companion object {
        /** Mensagem simples (antes: AlertDialog.setMessage). */
        fun message(
            c: Context, title: String, msg: String,
            okLabel: String = "OK", onOk: (() -> Unit)? = null,
            danger: Boolean = false
        ) {
            OniDialog(c)
                .title(title)
                .content(TextView(c).apply {
                    this.text = msg
                    setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                    setTextColor(Oni.TEXT)
                    setLineSpacing(Oni.dp(c, 2).toFloat(), 1f)
                })
                .button(Btn(okLabel, accent = !danger, onClick = onOk))
                .show()
        }

        /** Input 1 campo (antes: inputDialog + EditText default). */
        fun input(
            c: Context, title: String, initial: String,
            hint: String = "", mono: Boolean = false,
            numeric: Boolean = false,
            onOk: (String) -> Unit
        ) {
            val field = Oni.field(c, mono = mono)
            field.setSingleLine(true)
            field.setText(initial)
            field.hint = hint
            if (numeric) {
                field.inputType = android.text.InputType.TYPE_CLASS_NUMBER or
                    android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL or
                    android.text.InputType.TYPE_NUMBER_FLAG_SIGNED
            }
            field.imeOptions = android.view.inputmethod.EditorInfo.IME_ACTION_DONE
            OniDialog(c)
                .title(title)
                .content(field)
                .button(Btn("Cancelar", accent = false))
                .button(Btn("OK") { onOk(field.text.toString()) })
                .show()
        }

        /** Confirm destrutivo com OK vermelho explícito. */
        fun dangerConfirm(
            c: Context, title: String, msg: String,
            okLabel: String = "Apagar", onOk: () -> Unit
        ) {
            OniDialog(c)
                .title(title)
                .content(TextView(c).apply {
                    this.text = msg
                    setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                    setTextColor(Oni.TEXT)
                    setLineSpacing(Oni.dp(c, 2).toFloat(), 1f)
                })
                .button(Btn("Cancelar", accent = false))
                .button(Btn(okLabel, accent = false, danger = true,
                            onClick = onOk))
                .show()
        }

        /**
         * Menu de itens (antes: AlertDialog.setItems) — rows curvas 48dp,
         * scroll quando longo.
         */
        fun list(
            c: Context, title: String, items: List<String>,
            dangerIndex: Int = -1, onSelect: (Int) -> Unit
        ): Dialog {
            val column = LinearLayout(c).apply {
                orientation = LinearLayout.VERTICAL
            }
            val scroll = android.widget.ScrollView(c).apply {
                addView(column)
            }
            // rows fecham o diálogo (dlg é atribuído antes de qualquer toque
            // chegar — listeners só disparam após show()). Sem divisores:
            // separação por tom+espaço apenas (§1.1).
            var dlg: Dialog? = null
            for ((i, item) in items.withIndex()) {
                val row = Oni.listRow(c)
                row.addView(TextView(c).apply {
                    text = item
                    setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                    setTextColor(if (i == dangerIndex) Oni.DANGER else Oni.TEXT)
                    setPadding(0, Oni.dp(c, 12), 0, Oni.dp(c, 12))
                }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
                row.setOnClickListener {
                    dlg?.dismiss()
                    onSelect(i)
                }
                column.addView(row, LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT))
            }
            return OniDialog(c)
                .title(title)
                .content(scroll)
                .button(Btn("Cancelar", accent = false))
                .show()
        }

        /** Single-choice (antes: setSingleChoiceItems) — marcador "●/○". */
        fun choice(
            c: Context, title: String, items: List<String>,
            checked: Int, onSelect: (Int) -> Unit
        ) {
            val column = LinearLayout(c).apply {
                orientation = LinearLayout.VERTICAL
            }
            val scroll = android.widget.ScrollView(c).apply {
                addView(column)
            }
            var dlg: Dialog? = null
            for ((i, item) in items.withIndex()) {
                val row = Oni.listRow(c)
                row.addView(TextView(c).apply {
                    text = if (i == checked) "●" else "○"
                    setTextColor(if (i == checked) Oni.ACCENT else Oni.TEXT_DIM)
                    setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                    setPadding(0, 0, Oni.dp(c, 12), 0)
                })
                row.addView(TextView(c).apply {
                    text = item
                    setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                    setTextColor(Oni.TEXT)
                    setPadding(0, Oni.dp(c, 12), 0, Oni.dp(c, 12))
                }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
                row.setOnClickListener {
                    dlg?.dismiss()
                    onSelect(i)
                }
                column.addView(row, LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT))
            }
            dlg = OniDialog(c)
                .title(title)
                .content(scroll)
                .button(Btn("Cancelar", accent = false))
                .show()
        }

        /** Diálogo custom com conteúdo arbitrário + botões. */
        fun custom(
            c: Context, title: String, view: View,
            btns: List<Btn>
        ): Dialog =
            OniDialog(c)
                .title(title)
                .content(view)
                .apply { btns.forEach { button(it) } }
                .show()
    }
}
