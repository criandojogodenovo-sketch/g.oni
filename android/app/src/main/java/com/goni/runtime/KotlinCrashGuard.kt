package com.goni.runtime

import android.content.Context
import android.util.Log
import java.io.File

/**
 * P4.5.1 — Handler de exceções Kotlin (a rede de diagnóstico que faltava).
 *
 * O PROBLEMA REAL (Realme C33, APK P4.5 "Curved Dark"): 5 mortes de
 * startup e o handler existente só apanhava SINAIS NATIVOS. Exceções
 * Kotlin (NullPointerException, NoClassDefFoundError, Resources.NotFound…)
 * na camada reescrita pelo P4.5 matavam o processo SEM NENHUMA evidência
 * — nem goni_crash.log, nem tombstone nomeando a linha. Duas janelas de
 * morte ficaram cegas: ACTIVITY→EDITOR_HOST e pós-EDITOR_DOCUMENT (build
 * da UI nova). Este objeto fecha a lacuna.
 *
 * CONTRATO (P4.5.1 — zero mudança de comportamento em caminho saudável):
 *  1. Instalação uma vez por processo (idempotente — Activity recriada
 *     NÃO re-encadeia handlers: a cadeia guard→prev é preservada);
 *  2. Em crash: escreve EVIDÊNCIA completa em filesDir/goni_crash.log com
 *     a MESMA assinatura "[crash]" do handler nativo — de graça, o
 *     pipeline existente trata crash Kotlin igual ao nativo
 *     (hasPreviousCrashReport → export automático p/ Download/GONI na
 *     execução seguinte + crash-prompt);
 *  3. Espelho público best-effort e BORNED (exportCrashLogIfPresent —
 *     fila assíncrona com prazo de 2 s): a main morre, mas não ANTES de
 *     tentar a cópia;
 *  4. SEMPRE delega ao handler anterior (o comportamento de morte do
 *     sistema — diálogo do Android, ART, tombstone — é INTACTO; só
 *     ADICIONAMOS forense antes);
 *  5. FASE corrente ([phase]): os micro-marks R3 do onCreate/buildUi
 *     registram o estágio Kotlin em curso — o relatório nomeia a fase
 *     e o stack nomeia a LINHA.
 *
 * Tudo aqui é best-effort: qualquer falha dentro do guard nunca impede
 * a delegação (o processo morre como morreria — agora com evidência).
 */
object KotlinCrashGuard {

    private const val TAG = "GONI"
    private const val CRASH_LOG = "goni_crash.log"

    /** Fase Kotlin corrente (atualizada pelos micro-marks R3). */
    @Volatile private var currentPhase: String = "pré-install"

    /** Handler anterior (sempre delegado — comportamento do sistema intacto). */
    private var previousHandler: Thread.UncaughtExceptionHandler? = null

    @Volatile private var appContext: Context? = null

    @Volatile private var installed = false

    /** Registra a fase corrente (barato: uma escrita volátil). */
    fun phase(name: String) {
        currentPhase = name
    }

    /**
     * Instala o guard UMA vez por processo. Chamado no onCreate ANTES de
     * qualquer coisa que possa lançar (logo após DiagnosticsMirror.init —
     * o contexto do espelho precisa existir para o mirror borned).
     */
    fun install(context: Context) {
        if (installed) {
            return  // Activity recriada: NÃO re-encadeia (cadeia cresceria)
        }
        installed = true
        appContext = context.applicationContext
        previousHandler = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            try {
                writeForensics(thread, throwable)
            } catch (_: Throwable) {
                // A evidência é best-effort — nunca substitui a morte real.
            }
            try {
                mirrorBestEffort()
            } catch (_: Throwable) {
                // idem
            }
            // DELEGAÇÃO SEMPRE: o sistema vê o crash como sempre viu
            // (diálogo "app parou", ART, tombstone) — só agora com
            // forense já em disco.
            previousHandler?.uncaughtException(thread, throwable)
        }
    }

    /**
     * Evidência completa em filesDir/goni_crash.log — MESMO arquivo do
     * handler nativo, MESMO gatilho "[crash]" (linha 1):
     *   [crash] kotlin | phase=UI_SHEETS | thread=main | time=... |
     *           java.lang.NullPointerException | msg
     * Segue o stack completo (a linha que o C33 nunca conseguiu mostrar).
     * I/O puro em Java: válido aqui — NÃO estamos em contexto de sinal
     * (a exceção Kotlin corre no frame normal da JVM).
     */
    private fun writeForensics(thread: Thread, t: Throwable) {
        val ctx = appContext
        val stack = try { t.stackTraceToString() } catch (_: Throwable) {
            "<stack indisponível>"
        }
        // Logcat PRIMEIRO: se houver ADB ligado, a linha chega em vivo.
        Log.e(TAG, "[CRASH-KOTLIN] fase=${currentPhase} " +
            "thread=${thread.name} ${t.javaClass.name}: ${t.message}\n$stack")
        if (ctx == null) {
            return  // crash antes do install ter contexto: logcat é tudo
        }
        val header =
            "[crash] kotlin | phase=$currentPhase | thread=${thread.name} | " +
            "time=${System.currentTimeMillis()} | " +
            "${t.javaClass.name} | ${t.message ?: "-"}\n"
        val f = File(ctx.filesDir, CRASH_LOG)
        f.parentFile?.mkdirs()
        f.appendText(header + stack + "\n")
    }

    /**
     * Cópia pública IMEDIATA (Download/GONI) via a fila assíncrona já
     * testada do espelho — borned em 2 s (garantia crash-loop: a main
     * não fica presa num binder moribundo; a cópia privada em filesDir
     * já garante a evidência na execução seguinte de qualquer forma).
     */
    private fun mirrorBestEffort() {
        DiagnosticsMirror.exportCrashLogIfPresent()
    }
}
